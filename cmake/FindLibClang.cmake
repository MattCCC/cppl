# Locates a Clang installation that provides both:
#
#   * libclang  - the stable C API used by the C++L Clang semantic bridge;
#   * clang++   - the driver used for preprocessing and native code generation.
#
# Both must come from the same installation: the C++ semantics C++L verifies
# are the semantics that installation implements.
#
# Configuration inputs:
#   CPPL_LLVM_ROOT  - explicit installation prefix to use first.

set(_cppl_llvm_hints)

if(CPPL_LLVM_ROOT)
    list(APPEND _cppl_llvm_hints "${CPPL_LLVM_ROOT}")
endif()

if(DEFINED ENV{CPPL_LLVM_ROOT})
    list(APPEND _cppl_llvm_hints "$ENV{CPPL_LLVM_ROOT}")
endif()

find_program(_cppl_llvm_config
    NAMES llvm-config llvm-config-22 llvm-config-21 llvm-config-20 llvm-config-19 llvm-config-18
    HINTS ${_cppl_llvm_hints}
    PATH_SUFFIXES bin
    PATHS
        /opt/homebrew/opt/llvm/bin
        /opt/homebrew/opt/llvm@22/bin
        /opt/homebrew/opt/llvm@21/bin
        /opt/homebrew/opt/llvm@20/bin
        /usr/local/opt/llvm/bin
        /usr/lib/llvm-22/bin
        /usr/lib/llvm-21/bin
        /usr/lib/llvm-20/bin)

if(_cppl_llvm_config)
    execute_process(COMMAND "${_cppl_llvm_config}" --prefix
        OUTPUT_VARIABLE _cppl_llvm_prefix OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(COMMAND "${_cppl_llvm_config}" --version
        OUTPUT_VARIABLE LibClang_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    list(APPEND _cppl_llvm_hints "${_cppl_llvm_prefix}")
endif()

find_path(LibClang_INCLUDE_DIR
    NAMES clang-c/Index.h
    HINTS ${_cppl_llvm_hints}
    PATH_SUFFIXES include)

find_library(LibClang_LIBRARY
    NAMES clang libclang
    HINTS ${_cppl_llvm_hints}
    PATH_SUFFIXES lib lib64)

find_program(LibClang_CLANG_EXECUTABLE
    NAMES clang++
    HINTS ${_cppl_llvm_hints}
    PATH_SUFFIXES bin)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibClang
    REQUIRED_VARS LibClang_INCLUDE_DIR LibClang_LIBRARY LibClang_CLANG_EXECUTABLE
    VERSION_VAR LibClang_VERSION)

if(LibClang_FOUND AND NOT TARGET LibClang::LibClang)
    add_library(LibClang::LibClang UNKNOWN IMPORTED)
    set_target_properties(LibClang::LibClang PROPERTIES
        IMPORTED_LOCATION "${LibClang_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LibClang_INCLUDE_DIR}")
    cmake_path(GET LibClang_LIBRARY PARENT_PATH LibClang_LIBRARY_DIR)
endif()

mark_as_advanced(LibClang_INCLUDE_DIR LibClang_LIBRARY LibClang_CLANG_EXECUTABLE _cppl_llvm_config)
