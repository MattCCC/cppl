# cmake/FindLibClang.cmake
#
# Locate one coherent LLVM/Clang installation for C++L.
#
# Exports:
#
#   LibClang_FOUND
#   LibClang_VERSION
#   LibClang_ROOT_DIR
#   LibClang_INCLUDE_DIR
#   LibClang_INCLUDE_DIRS
#   LibClang_LIBRARY
#   LibClang_LIBRARIES
#   LibClang_CLANG_EXECUTABLE
#
# Imported target:
#
#   LibClang::LibClang
#
# Optional configuration:
#
#   -DLibClang_ROOT=/path/to/llvm
#   -DLibClang_PREFERRED_MAJOR=22
#
# Invariants:
#
#   - clang-c/Index.h
#   - libclang
#   - clang++ / clang-cl
#
# must come from the same LLVM installation.
#
# Discovery order:
#
#   1. Explicit LibClang_ROOT
#   2. Environment hints
#   3. Existing Clang_DIR / LLVM_DIR
#   4. Official Clang CMake package
#   5. llvm-config
#   6. Homebrew, when running on macOS
#   7. Standard LLVM locations on Linux
#   8. Standard LLVM location on Windows
#   9. Matching Clang driver on PATH
#  10. Generic clang-c header discovery
#
# An explicitly supplied LibClang_ROOT is authoritative. If it is invalid,
# configuration fails rather than silently selecting another installation.

include_guard(GLOBAL)

include(FindPackageHandleStandardArgs)

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------

set(
    LibClang_ROOT
    ""
    CACHE PATH
    "Root of the LLVM/Clang installation used by C++L"
)

set(
    LibClang_PREFERRED_MAJOR
    "22"
    CACHE STRING
    "Preferred LLVM major version; empty accepts any coherent installation"
)

# -----------------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------------

function(
    _cppl_libclang_normalize_path
    INPUT_PATH
    OUTPUT_VAR
)
    if(NOT INPUT_PATH)
        set(
            ${OUTPUT_VAR}
            ""
            PARENT_SCOPE
        )

        return()
    endif()

    get_filename_component(
        _path
        "${INPUT_PATH}"
        ABSOLUTE
    )

    file(
        TO_CMAKE_PATH
        "${_path}"
        _path
    )

    set(
        ${OUTPUT_VAR}
        "${_path}"
        PARENT_SCOPE
    )
endfunction()

function(
    _cppl_libclang_root_from_config_dir
    CONFIG_DIR
    OUTPUT_VAR
)
    if(NOT CONFIG_DIR)
        set(
            ${OUTPUT_VAR}
            ""
            PARENT_SCOPE
        )

        return()
    endif()

    # Expected layout:
    #
    #   <root>/lib/cmake/clang
    #   <root>/lib/cmake/llvm

    get_filename_component(
        _p1
        "${CONFIG_DIR}"
        DIRECTORY
    )

    get_filename_component(
        _p2
        "${_p1}"
        DIRECTORY
    )

    get_filename_component(
        _p3
        "${_p2}"
        DIRECTORY
    )

    _cppl_libclang_normalize_path(
        "${_p3}"
        _root
    )

    set(
        ${OUTPUT_VAR}
        "${_root}"
        PARENT_SCOPE
    )
endfunction()

function(
    _cppl_libclang_driver_version
    DRIVER
    OUTPUT_VERSION
    OUTPUT_MAJOR
)
    execute_process(
        COMMAND
            "${DRIVER}"
            --version

        RESULT_VARIABLE
            _rc

        OUTPUT_VARIABLE
            _out

        ERROR_VARIABLE
            _err

        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )

    if(NOT _rc EQUAL 0)
        set(
            ${OUTPUT_VERSION}
            ""
            PARENT_SCOPE
        )

        set(
            ${OUTPUT_MAJOR}
            ""
            PARENT_SCOPE
        )

        return()
    endif()

    string(
        REGEX MATCH
        "[Vv]ersion[ \t]+([0-9]+)(\\.[0-9]+)?(\\.[0-9]+)?"
        _match
        "${_out}\n${_err}"
    )

    if(NOT CMAKE_MATCH_1)
        set(
            ${OUTPUT_VERSION}
            ""
            PARENT_SCOPE
        )

        set(
            ${OUTPUT_MAJOR}
            ""
            PARENT_SCOPE
        )

        return()
    endif()

    set(
        _major
        "${CMAKE_MATCH_1}"
    )

    string(
        REGEX MATCH
        "[0-9]+(\\.[0-9]+)?(\\.[0-9]+)?"
        _version
        "${_match}"
    )

    set(
        ${OUTPUT_VERSION}
        "${_version}"
        PARENT_SCOPE
    )

    set(
        ${OUTPUT_MAJOR}
        "${_major}"
        PARENT_SCOPE
    )
endfunction()

function(
    _cppl_libclang_probe_root
    ROOT
    OUTPUT_OK
    OUTPUT_INCLUDE
    OUTPUT_LIBRARY
    OUTPUT_DRIVER
    OUTPUT_VERSION
)
    _cppl_libclang_normalize_path(
        "${ROOT}"
        _root
    )

    if(
        NOT _root
        OR
        NOT IS_DIRECTORY "${_root}"
    )
        set(
            ${OUTPUT_OK}
            FALSE
            PARENT_SCOPE
        )

        return()
    endif()

    # -------------------------------------------------------------------------
    # Header
    # -------------------------------------------------------------------------

    set(
        _include
        "${_root}/include"
    )

    if(NOT EXISTS "${_include}/clang-c/Index.h")
        set(
            ${OUTPUT_OK}
            FALSE
            PARENT_SCOPE
        )

        return()
    endif()

    # -------------------------------------------------------------------------
    # libclang
    # -------------------------------------------------------------------------

    find_library(
        _library

        NAMES
            clang
            libclang

        HINTS
            "${_root}/lib"
            "${_root}/lib64"
            "${_root}/bin"

        NO_DEFAULT_PATH
        NO_CACHE
    )

    if(NOT _library)
        set(
            ${OUTPUT_OK}
            FALSE
            PARENT_SCOPE
        )

        return()
    endif()

    # -------------------------------------------------------------------------
    # Clang driver
    # -------------------------------------------------------------------------

    set(_driver "")

    if(WIN32)
        set(
            _driver_names
            clang++.exe
            clang-cl.exe
            clang.exe
        )
    else()
        set(
            _driver_names
            clang++
            clang
        )
    endif()

    foreach(_name IN LISTS _driver_names)
        set(
            _candidate
            "${_root}/bin/${_name}"
        )

        if(
            EXISTS "${_candidate}"
            AND
            NOT IS_DIRECTORY "${_candidate}"
        )
            set(
                _driver
                "${_candidate}"
            )

            break()
        endif()
    endforeach()

    if(NOT _driver)
        set(
            ${OUTPUT_OK}
            FALSE
            PARENT_SCOPE
        )

        return()
    endif()

    # -------------------------------------------------------------------------
    # Version
    # -------------------------------------------------------------------------

    _cppl_libclang_driver_version(
        "${_driver}"
        _version
        _major
    )

    if(NOT _version)
        set(
            ${OUTPUT_OK}
            FALSE
            PARENT_SCOPE
        )

        return()
    endif()

    if(
        LibClang_PREFERRED_MAJOR
        AND
        NOT _major STREQUAL LibClang_PREFERRED_MAJOR
    )
        set(
            ${OUTPUT_OK}
            FALSE
            PARENT_SCOPE
        )

        return()
    endif()

    # -------------------------------------------------------------------------
    # Success
    # -------------------------------------------------------------------------

    set(
        ${OUTPUT_OK}
        TRUE
        PARENT_SCOPE
    )

    set(
        ${OUTPUT_INCLUDE}
        "${_include}"
        PARENT_SCOPE
    )

    set(
        ${OUTPUT_LIBRARY}
        "${_library}"
        PARENT_SCOPE
    )

    set(
        ${OUTPUT_DRIVER}
        "${_driver}"
        PARENT_SCOPE
    )

    set(
        ${OUTPUT_VERSION}
        "${_version}"
        PARENT_SCOPE
    )
endfunction()

# -----------------------------------------------------------------------------
# Candidate roots
# -----------------------------------------------------------------------------

set(_LibClang_CANDIDATE_ROOTS)

macro(
    _cppl_libclang_add_root
    ROOT_VALUE
)
    if(NOT "${ROOT_VALUE}" STREQUAL "")
        _cppl_libclang_normalize_path(
            "${ROOT_VALUE}"
            _candidate_root
        )

        if(_candidate_root)
            list(
                APPEND
                _LibClang_CANDIDATE_ROOTS
                "${_candidate_root}"
            )
        endif()
    endif()
endmacro()

# -----------------------------------------------------------------------------
# Explicit root
# -----------------------------------------------------------------------------

set(
    _LibClang_EXPLICIT_ROOT
    FALSE
)

if(LibClang_ROOT)
    set(
        _LibClang_EXPLICIT_ROOT
        TRUE
    )

    _cppl_libclang_add_root(
        "${LibClang_ROOT}"
    )
endif()

# -----------------------------------------------------------------------------
# Automatic discovery
# -----------------------------------------------------------------------------

if(NOT _LibClang_EXPLICIT_ROOT)

    # -------------------------------------------------------------------------
    # Environment
    # -------------------------------------------------------------------------

    if(DEFINED ENV{LIBCLANG_ROOT})
        _cppl_libclang_add_root(
            "$ENV{LIBCLANG_ROOT}"
        )
    endif()

    if(DEFINED ENV{LLVM_ROOT})
        _cppl_libclang_add_root(
            "$ENV{LLVM_ROOT}"
        )
    endif()

    if(DEFINED ENV{LLVM_HOME})
        _cppl_libclang_add_root(
            "$ENV{LLVM_HOME}"
        )
    endif()

    # -------------------------------------------------------------------------
    # Existing CMake package hints
    # -------------------------------------------------------------------------

    if(Clang_DIR)
        _cppl_libclang_root_from_config_dir(
            "${Clang_DIR}"
            _root_from_clang_dir
        )

        _cppl_libclang_add_root(
            "${_root_from_clang_dir}"
        )
    endif()

    if(LLVM_DIR)
        _cppl_libclang_root_from_config_dir(
            "${LLVM_DIR}"
            _root_from_llvm_dir
        )

        _cppl_libclang_add_root(
            "${_root_from_llvm_dir}"
        )
    endif()

    if(LLVM_TOOLS_BINARY_DIR)
        get_filename_component(
            _root_from_tools
            "${LLVM_TOOLS_BINARY_DIR}"
            DIRECTORY
        )

        _cppl_libclang_add_root(
            "${_root_from_tools}"
        )
    endif()

    # -------------------------------------------------------------------------
    # Official Clang CMake package
    # -------------------------------------------------------------------------

    find_package(
        Clang
        CONFIG
        QUIET
    )

    if(Clang_FOUND)
        if(LLVM_TOOLS_BINARY_DIR)
            get_filename_component(
                _root_from_clang_package
                "${LLVM_TOOLS_BINARY_DIR}"
                DIRECTORY
            )

            _cppl_libclang_add_root(
                "${_root_from_clang_package}"
            )

        elseif(Clang_DIR)
            _cppl_libclang_root_from_config_dir(
                "${Clang_DIR}"
                _root_from_clang_package
            )

            _cppl_libclang_add_root(
                "${_root_from_clang_package}"
            )
        endif()
    endif()

    # -------------------------------------------------------------------------
    # llvm-config
    # -------------------------------------------------------------------------

    set(
        _llvm_config_names
        llvm-config
    )

    if(LibClang_PREFERRED_MAJOR)
        list(
            PREPEND
            _llvm_config_names
            "llvm-config-${LibClang_PREFERRED_MAJOR}"
        )
    endif()

    find_program(
        _LibClang_LLVM_CONFIG

        NAMES
            ${_llvm_config_names}

        NO_CACHE
    )

    if(_LibClang_LLVM_CONFIG)
        execute_process(
            COMMAND
                "${_LibClang_LLVM_CONFIG}"
                --prefix

            RESULT_VARIABLE
                _llvm_config_rc

            OUTPUT_VARIABLE
                _llvm_config_prefix

            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(_llvm_config_rc EQUAL 0)
            _cppl_libclang_add_root(
                "${_llvm_config_prefix}"
            )
        endif()
    endif()

    # -------------------------------------------------------------------------
    # macOS / Homebrew
    # -------------------------------------------------------------------------
    #
    # Homebrew is queried only when it actually exists.
    #
    # No /opt/homebrew, /usr/local, Cellar path, or formula installation path
    # is hard-coded.

    if(APPLE)
        find_program(
            _LibClang_BREW_EXECUTABLE

            NAMES
                brew

            NO_CACHE
        )

        if(_LibClang_BREW_EXECUTABLE)
            execute_process(
                COMMAND
                    "${_LibClang_BREW_EXECUTABLE}"
                    list
                    --formula

                RESULT_VARIABLE
                    _brew_list_rc

                OUTPUT_VARIABLE
                    _brew_formulae_raw

                OUTPUT_STRIP_TRAILING_WHITESPACE
            )

            if(_brew_list_rc EQUAL 0)
                string(
                    REPLACE
                    "\n"
                    ";"
                    _brew_formulae
                    "${_brew_formulae_raw}"
                )

                set(_brew_llvm_formulae)

                foreach(_formula IN LISTS _brew_formulae)
                    if(_formula MATCHES "^llvm(@[0-9]+)?$")
                        list(
                            APPEND
                            _brew_llvm_formulae
                            "${_formula}"
                        )
                    endif()
                endforeach()

                # Prefer the pinned major when installed.
                if(LibClang_PREFERRED_MAJOR)
                    set(
                        _preferred_formula
                        "llvm@${LibClang_PREFERRED_MAJOR}"
                    )

                    list(
                        FIND
                        _brew_llvm_formulae
                        "${_preferred_formula}"
                        _preferred_index
                    )

                    if(NOT _preferred_index EQUAL -1)
                        list(
                            REMOVE_ITEM
                            _brew_llvm_formulae
                            "${_preferred_formula}"
                        )

                        list(
                            PREPEND
                            _brew_llvm_formulae
                            "${_preferred_formula}"
                        )
                    endif()
                endif()

                foreach(_formula IN LISTS _brew_llvm_formulae)
                    execute_process(
                        COMMAND
                            "${_LibClang_BREW_EXECUTABLE}"
                            --prefix
                            "${_formula}"

                        RESULT_VARIABLE
                            _brew_prefix_rc

                        OUTPUT_VARIABLE
                            _brew_prefix

                        OUTPUT_STRIP_TRAILING_WHITESPACE
                    )

                    if(_brew_prefix_rc EQUAL 0)
                        _cppl_libclang_add_root(
                            "${_brew_prefix}"
                        )
                    endif()
                endforeach()
            endif()
        endif()
    endif()

    # -------------------------------------------------------------------------
    # Linux / Unix LLVM installations
    # -------------------------------------------------------------------------

    if(
        UNIX
        AND
        NOT APPLE
    )
        file(
            GLOB
            _llvm_unix_roots

            LIST_DIRECTORIES
            TRUE

            "/usr/lib/llvm-*"
            "/usr/local/lib/llvm-*"
        )

        if(_llvm_unix_roots)
            list(
                SORT
                _llvm_unix_roots

                COMPARE
                NATURAL

                ORDER
                DESCENDING
            )

            foreach(_root IN LISTS _llvm_unix_roots)
                _cppl_libclang_add_root(
                    "${_root}"
                )
            endforeach()
        endif()
    endif()

    # -------------------------------------------------------------------------
    # Windows
    # -------------------------------------------------------------------------

    if(WIN32)
        if(DEFINED ENV{ProgramFiles})
            _cppl_libclang_add_root(
                "$ENV{ProgramFiles}/LLVM"
            )
        endif()
    endif()

    # -------------------------------------------------------------------------
    # Clang on PATH
    # -------------------------------------------------------------------------

    set(
        _clang_driver_names
        clang++
        clang-cl
        clang
    )

    if(
        LibClang_PREFERRED_MAJOR
        AND
        NOT WIN32
    )
        list(
            PREPEND
            _clang_driver_names
            "clang++-${LibClang_PREFERRED_MAJOR}"
            "clang-${LibClang_PREFERRED_MAJOR}"
        )
    endif()

    find_program(
        _LibClang_PATH_DRIVER

        NAMES
            ${_clang_driver_names}

        NO_CACHE
    )

    if(_LibClang_PATH_DRIVER)
        get_filename_component(
            _path_driver_real
            "${_LibClang_PATH_DRIVER}"
            REALPATH
        )

        get_filename_component(
            _path_driver_bin
            "${_path_driver_real}"
            DIRECTORY
        )

        get_filename_component(
            _path_driver_root
            "${_path_driver_bin}"
            DIRECTORY
        )

        _cppl_libclang_add_root(
            "${_path_driver_root}"
        )
    endif()

    # -------------------------------------------------------------------------
    # Generic header discovery
    # -------------------------------------------------------------------------
    #
    # We never accept the header alone.
    #
    # Its root is inferred and then passed through the same coherent-root
    # validation as every other candidate.

    find_path(
        _LibClang_GENERIC_INCLUDE

        NAMES
            clang-c/Index.h

        NO_CACHE
    )

    if(_LibClang_GENERIC_INCLUDE)
        get_filename_component(
            _generic_root
            "${_LibClang_GENERIC_INCLUDE}"
            DIRECTORY
        )

        _cppl_libclang_add_root(
            "${_generic_root}"
        )
    endif()

endif()

# -----------------------------------------------------------------------------
# Probe candidates
# -----------------------------------------------------------------------------

list(
    REMOVE_DUPLICATES
    _LibClang_CANDIDATE_ROOTS
)

set(LibClang_ROOT_DIR "")
set(LibClang_INCLUDE_DIR "")
set(LibClang_LIBRARY "")
set(LibClang_CLANG_EXECUTABLE "")
set(LibClang_VERSION "")

foreach(_root IN LISTS _LibClang_CANDIDATE_ROOTS)
    _cppl_libclang_probe_root(
        "${_root}"
        _ok
        _include
        _library
        _driver
        _version
    )

    if(_ok)
        set(
            LibClang_ROOT_DIR
            "${_root}"
        )

        set(
            LibClang_INCLUDE_DIR
            "${_include}"
        )

        set(
            LibClang_LIBRARY
            "${_library}"
        )

        set(
            LibClang_CLANG_EXECUTABLE
            "${_driver}"
        )

        set(
            LibClang_VERSION
            "${_version}"
        )

        break()
    endif()
endforeach()

set(
    LibClang_INCLUDE_DIRS
    "${LibClang_INCLUDE_DIR}"
)

set(
    LibClang_LIBRARIES
    "${LibClang_LIBRARY}"
)

# -----------------------------------------------------------------------------
# Explicit-root failure
# -----------------------------------------------------------------------------

if(
    _LibClang_EXPLICIT_ROOT
    AND
    NOT LibClang_ROOT_DIR
)
    if(LibClang_PREFERRED_MAJOR)
        set(
            _expected_version
            " LLVM ${LibClang_PREFERRED_MAJOR}"
        )
    else()
        set(
            _expected_version
            ""
        )
    endif()

    message(
        FATAL_ERROR
        "LibClang_ROOT does not name a coherent${_expected_version} installation:\n"
        "  ${LibClang_ROOT}\n"
        "\n"
        "Expected under that root:\n"
        "  include/clang-c/Index.h\n"
        "  lib/libclang (or platform equivalent)\n"
        "  bin/clang++ (or clang-cl/clang on Windows)"
    )
endif()

# -----------------------------------------------------------------------------
# Standard find_package result
# -----------------------------------------------------------------------------

find_package_handle_standard_args(
    LibClang

    REQUIRED_VARS
        LibClang_ROOT_DIR
        LibClang_INCLUDE_DIR
        LibClang_LIBRARY
        LibClang_CLANG_EXECUTABLE

    VERSION_VAR
        LibClang_VERSION
)

# -----------------------------------------------------------------------------
# Imported target
# -----------------------------------------------------------------------------

if(
    LibClang_FOUND
    AND
    NOT TARGET LibClang::LibClang
)
    add_library(
        LibClang::LibClang
        UNKNOWN
        IMPORTED
    )

    set_target_properties(
        LibClang::LibClang

        PROPERTIES
            IMPORTED_LOCATION
                "${LibClang_LIBRARY}"

            INTERFACE_INCLUDE_DIRECTORIES
                "${LibClang_INCLUDE_DIR}"
    )
endif()

# -----------------------------------------------------------------------------
# Cache/public result variables
# -----------------------------------------------------------------------------

if(LibClang_FOUND)
    set(
        LibClang_ROOT_DIR
        "${LibClang_ROOT_DIR}"
        CACHE PATH
        "Resolved LLVM/Clang installation root"
        FORCE
    )

    set(
        LibClang_INCLUDE_DIR
        "${LibClang_INCLUDE_DIR}"
        CACHE PATH
        "Directory containing clang-c/Index.h"
        FORCE
    )

    set(
        LibClang_LIBRARY
        "${LibClang_LIBRARY}"
        CACHE FILEPATH
        "libclang library"
        FORCE
    )

    set(
        LibClang_CLANG_EXECUTABLE
        "${LibClang_CLANG_EXECUTABLE}"
        CACHE FILEPATH
        "Clang driver matching libclang"
        FORCE
    )

    message(
        STATUS
        "C++L: LLVM root    ${LibClang_ROOT_DIR}"
    )

    message(
        STATUS
        "C++L: LLVM version ${LibClang_VERSION}"
    )
endif()

# -----------------------------------------------------------------------------
# Hide implementation details from ordinary CMake GUIs
# -----------------------------------------------------------------------------

mark_as_advanced(
    LibClang_INCLUDE_DIR
    LibClang_LIBRARY
    LibClang_CLANG_EXECUTABLE
    LibClang_ROOT_DIR
)