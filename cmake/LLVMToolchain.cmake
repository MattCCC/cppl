# cmake/LLVMToolchain.cmake
#
# Central LLVM tool discovery for C++L.
#
# Invariants:
#
#   1. C++L selects one authoritative Clang driver.
#   2. Auxiliary LLVM tools should come from that same installation.
#   3. If that is impossible, a fallback tool must match the selected
#      Clang driver's LLVM major version.
#   4. Explicit per-tool overrides are supported for unusual installations.
#   5. No platform-specific Homebrew/Linux/Windows paths are hard-coded.
#
# Expected before inclusion:
#
#   CPPL_CLANG_DRIVER
#
# Example:
#
#   /opt/homebrew/.../bin/clang++
#   /usr/lib/llvm-22/bin/clang++
#   C:/Program Files/LLVM/bin/clang++.exe
#
# Usage:
#
#   include(LLVMToolchain)
#
#   cppl_find_llvm_tool(CPPL_CLANG_FORMAT clang-format)
#   cppl_find_llvm_tool(CPPL_CLANG_TIDY   clang-tidy)
#
# Optional explicit override:
#
#   cmake --preset dev \
#       -DCPPL_CLANG_FORMAT=/custom/path/clang-format
#
#   cmake --preset dev \
#       -DCPPL_CLANG_TIDY=/custom/path/clang-tidy

include_guard(GLOBAL)

# -----------------------------------------------------------------------------
# Preconditions
# -----------------------------------------------------------------------------

if(
    NOT DEFINED CPPL_CLANG_DRIVER
    OR CPPL_CLANG_DRIVER STREQUAL ""
)
    message(
        FATAL_ERROR
        "CPPL_CLANG_DRIVER is not defined.\n"
        "\n"
        "The C++L Clang driver must be resolved before "
        "LLVMToolchain.cmake is included."
    )
endif()

if(NOT EXISTS "${CPPL_CLANG_DRIVER}")
    message(
        FATAL_ERROR
        "The selected C++L Clang driver does not exist:\n"
        "  ${CPPL_CLANG_DRIVER}"
    )
endif()

# -----------------------------------------------------------------------------
# Resolve the real Clang executable
# -----------------------------------------------------------------------------
#
# REALPATH is important when clang++ itself is a symlink.
# We want the actual LLVM installation, not merely the directory containing
# an arbitrary symlink placed on PATH.

get_filename_component(
    CPPL_CLANG_DRIVER_REAL
    "${CPPL_CLANG_DRIVER}"
    REALPATH
)

if(NOT CPPL_CLANG_DRIVER_REAL)
    set(
        CPPL_CLANG_DRIVER_REAL
        "${CPPL_CLANG_DRIVER}"
    )
endif()

file(
    TO_CMAKE_PATH
    "${CPPL_CLANG_DRIVER_REAL}"
    CPPL_CLANG_DRIVER_REAL
)

# -----------------------------------------------------------------------------
# Determine LLVM tools directory
# -----------------------------------------------------------------------------

get_filename_component(
    CPPL_LLVM_TOOLS_DIR
    "${CPPL_CLANG_DRIVER_REAL}"
    DIRECTORY
)

get_filename_component(
    CPPL_LLVM_TOOLS_DIR
    "${CPPL_LLVM_TOOLS_DIR}"
    ABSOLUTE
)

file(
    TO_CMAKE_PATH
    "${CPPL_LLVM_TOOLS_DIR}"
    CPPL_LLVM_TOOLS_DIR
)

set(
    CPPL_LLVM_TOOLS_DIR
    "${CPPL_LLVM_TOOLS_DIR}"
    CACHE INTERNAL
    "Directory containing LLVM tools selected by C++L"
    FORCE
)

# -----------------------------------------------------------------------------
# Determine selected LLVM major version
# -----------------------------------------------------------------------------

execute_process(
    COMMAND
        "${CPPL_CLANG_DRIVER_REAL}"
        --version

    RESULT_VARIABLE
        _CPPL_CLANG_VERSION_RESULT

    OUTPUT_VARIABLE
        _CPPL_CLANG_VERSION_OUTPUT

    ERROR_VARIABLE
        _CPPL_CLANG_VERSION_ERROR

    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
)

if(NOT _CPPL_CLANG_VERSION_RESULT EQUAL 0)
    message(
        FATAL_ERROR
        "Unable to query the selected Clang driver version:\n"
        "  ${CPPL_CLANG_DRIVER_REAL}\n"
        "\n"
        "${_CPPL_CLANG_VERSION_ERROR}"
    )
endif()

string(
    REGEX MATCH
    "[Vv]ersion[ \t]+([0-9]+)"
    _CPPL_CLANG_VERSION_MATCH
    "${_CPPL_CLANG_VERSION_OUTPUT}"
)

if(NOT CMAKE_MATCH_1)
    message(
        FATAL_ERROR
        "Unable to determine the LLVM major version from:\n"
        "  ${CPPL_CLANG_DRIVER_REAL}\n"
        "\n"
        "Version output:\n"
        "${_CPPL_CLANG_VERSION_OUTPUT}"
    )
endif()

set(
    CPPL_LLVM_MAJOR_VERSION
    "${CMAKE_MATCH_1}"
    CACHE INTERNAL
    "LLVM major version selected by C++L"
    FORCE
)

message(
    STATUS
    "C++L: LLVM tools   ${CPPL_LLVM_TOOLS_DIR}"
)

message(
    STATUS
    "C++L: LLVM major   ${CPPL_LLVM_MAJOR_VERSION}"
)

# -----------------------------------------------------------------------------
# Internal: determine the major version of an LLVM executable
# -----------------------------------------------------------------------------

function(
    _cppl_get_llvm_tool_major
    TOOL_PATH
    OUT_VAR
)
    execute_process(
        COMMAND
            "${TOOL_PATH}"
            --version

        RESULT_VARIABLE
            _result

        OUTPUT_VARIABLE
            _output

        ERROR_VARIABLE
            _error

        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )

    if(NOT _result EQUAL 0)
        set(
            "${OUT_VAR}"
            ""
            PARENT_SCOPE
        )

        return()
    endif()

    string(
        REGEX MATCH
        "[Vv]ersion[ \t]+([0-9]+)"
        _version_match
        "${_output}"
    )

    if(CMAKE_MATCH_1)
        set(
            "${OUT_VAR}"
            "${CMAKE_MATCH_1}"
            PARENT_SCOPE
        )
    else()
        set(
            "${OUT_VAR}"
            ""
            PARENT_SCOPE
        )
    endif()
endfunction()

# -----------------------------------------------------------------------------
# Internal: validate an LLVM tool
# -----------------------------------------------------------------------------

function(
    _cppl_validate_llvm_tool
    TOOL_PATH
    TOOL_NAME
)
    if(NOT EXISTS "${TOOL_PATH}")
        message(
            FATAL_ERROR
            "LLVM tool '${TOOL_NAME}' does not exist:\n"
            "  ${TOOL_PATH}"
        )
    endif()

    if(IS_DIRECTORY "${TOOL_PATH}")
        message(
            FATAL_ERROR
            "LLVM tool '${TOOL_NAME}' resolves to a directory:\n"
            "  ${TOOL_PATH}"
        )
    endif()

    _cppl_get_llvm_tool_major(
        "${TOOL_PATH}"
        _tool_major
    )

    if(NOT _tool_major)
        message(
            FATAL_ERROR
            "Unable to determine the version of LLVM tool '${TOOL_NAME}':\n"
            "  ${TOOL_PATH}"
        )
    endif()

    if(
        NOT _tool_major
        STREQUAL
        "${CPPL_LLVM_MAJOR_VERSION}"
    )
        message(
            FATAL_ERROR
            "LLVM tool version mismatch.\n"
            "\n"
            "C++L Clang driver:\n"
            "  ${CPPL_CLANG_DRIVER_REAL}\n"
            "  LLVM major: ${CPPL_LLVM_MAJOR_VERSION}\n"
            "\n"
            "${TOOL_NAME}:\n"
            "  ${TOOL_PATH}\n"
            "  LLVM major: ${_tool_major}\n"
            "\n"
            "C++L does not mix LLVM major versions."
        )
    endif()
endfunction()

# -----------------------------------------------------------------------------
# Public: resolve an LLVM tool
# -----------------------------------------------------------------------------

function(
    cppl_find_llvm_tool
    OUT_VAR
    TOOL_NAME
)
    if("${OUT_VAR}" STREQUAL "")
        message(
            FATAL_ERROR
            "cppl_find_llvm_tool requires an output variable"
        )
    endif()

    if("${TOOL_NAME}" STREQUAL "")
        message(
            FATAL_ERROR
            "cppl_find_llvm_tool requires a tool name"
        )
    endif()

    set(_tool "")

    # -------------------------------------------------------------------------
    # 1. Explicit user override
    # -------------------------------------------------------------------------
    #
    # Example:
    #
    #   -DCPPL_CLANG_FORMAT=/path/to/clang-format

    if(
        DEFINED ${OUT_VAR}
        AND NOT "${${OUT_VAR}}" STREQUAL ""
    )
        set(
            _tool
            "${${OUT_VAR}}"
        )

        _cppl_validate_llvm_tool(
            "${_tool}"
            "${TOOL_NAME}"
        )
    endif()

    # -------------------------------------------------------------------------
    # 2. Exact sibling executable
    # -------------------------------------------------------------------------

    if(NOT _tool)
        set(
            _candidate
            "${CPPL_LLVM_TOOLS_DIR}/${TOOL_NAME}${CMAKE_EXECUTABLE_SUFFIX}"
        )

        if(
            EXISTS "${_candidate}"
            AND NOT IS_DIRECTORY "${_candidate}"
        )
            set(
                _tool
                "${_candidate}"
            )
        endif()
    endif()

    # -------------------------------------------------------------------------
    # 3. Literal sibling executable
    # -------------------------------------------------------------------------
    #
    # Useful when CMAKE_EXECUTABLE_SUFFIX does not describe the host executable
    # naming convention being used.

    if(NOT _tool)
        set(
            _candidate
            "${CPPL_LLVM_TOOLS_DIR}/${TOOL_NAME}"
        )

        if(
            EXISTS "${_candidate}"
            AND NOT IS_DIRECTORY "${_candidate}"
        )
            set(
                _tool
                "${_candidate}"
            )
        endif()
    endif()

    # -------------------------------------------------------------------------
    # 4. Version-suffixed sibling
    # -------------------------------------------------------------------------
    #
    # Common on Linux distributions:
    #
    #   clang-format-22
    #   clang-tidy-22

    if(NOT _tool)
        set(
            _candidate
            "${CPPL_LLVM_TOOLS_DIR}/${TOOL_NAME}-${CPPL_LLVM_MAJOR_VERSION}${CMAKE_EXECUTABLE_SUFFIX}"
        )

        if(
            EXISTS "${_candidate}"
            AND NOT IS_DIRECTORY "${_candidate}"
        )
            set(
                _tool
                "${_candidate}"
            )
        endif()
    endif()

    # -------------------------------------------------------------------------
    # 5. Controlled PATH fallback
    # -------------------------------------------------------------------------
    #
    # This is allowed only if the discovered executable reports the same LLVM
    # major version as the authoritative C++L Clang driver.

    if(NOT _tool)
        unset(_CPPL_FOUND_TOOL)

        find_program(
            _CPPL_FOUND_TOOL

            NAMES
                "${TOOL_NAME}-${CPPL_LLVM_MAJOR_VERSION}"
                "${TOOL_NAME}"

            NO_CACHE
        )

        if(_CPPL_FOUND_TOOL)
            _cppl_get_llvm_tool_major(
                "${_CPPL_FOUND_TOOL}"
                _found_major
            )

            if(
                _found_major
                STREQUAL
                "${CPPL_LLVM_MAJOR_VERSION}"
            )
                set(
                    _tool
                    "${_CPPL_FOUND_TOOL}"
                )
            endif()
        endif()
    endif()

    # -------------------------------------------------------------------------
    # Failure
    # -------------------------------------------------------------------------

    if(NOT _tool)
        message(
            FATAL_ERROR
            "Required LLVM tool '${TOOL_NAME}' was not found.\n"
            "\n"
            "C++L selected:\n"
            "  Clang driver: ${CPPL_CLANG_DRIVER_REAL}\n"
            "  LLVM bin:     ${CPPL_LLVM_TOOLS_DIR}\n"
            "  LLVM major:   ${CPPL_LLVM_MAJOR_VERSION}\n"
            "\n"
            "Expected one of:\n"
            "  ${CPPL_LLVM_TOOLS_DIR}/${TOOL_NAME}${CMAKE_EXECUTABLE_SUFFIX}\n"
            "  ${CPPL_LLVM_TOOLS_DIR}/${TOOL_NAME}-${CPPL_LLVM_MAJOR_VERSION}${CMAKE_EXECUTABLE_SUFFIX}\n"
            "\n"
            "A matching tool was also not found on PATH.\n"
            "\n"
            "You may explicitly provide it with:\n"
            "  -D${OUT_VAR}=/path/to/${TOOL_NAME}"
        )
    endif()

    # -------------------------------------------------------------------------
    # Final validation
    # -------------------------------------------------------------------------

    get_filename_component(
        _tool
        "${_tool}"
        REALPATH
    )

    file(
        TO_CMAKE_PATH
        "${_tool}"
        _tool
    )

    _cppl_validate_llvm_tool(
        "${_tool}"
        "${TOOL_NAME}"
    )

    set(
        "${OUT_VAR}"
        "${_tool}"
        CACHE FILEPATH
        "LLVM ${TOOL_NAME} executable used by C++L"
        FORCE
    )

    set(
        "${OUT_VAR}"
        "${_tool}"
        PARENT_SCOPE
    )

    message(
        STATUS
        "C++L: ${TOOL_NAME} ${_tool}"
    )
endfunction()