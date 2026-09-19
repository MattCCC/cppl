# cmake/Linting.cmake
#
# Repository-wide clang-tidy integration.
#
# Provides:
#
#   lint
#       Run clang-tidy over all project C/C++ translation units using the
#       compilation database produced by CMake.
#
# clang-tidy is resolved by LLVMToolchain.cmake and must come from the same
# LLVM installation as the Clang driver selected by C++L.

include_guard(GLOBAL)

if(NOT COMMAND cppl_find_llvm_tool)
    message(
        FATAL_ERROR
        "Linting.cmake requires LLVMToolchain.cmake to be included first."
    )
endif()

cppl_find_llvm_tool(
    CPPL_CLANG_TIDY
    clang-tidy
)

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------

set(
    CPPL_CLANG_TIDY_CONFIG
    "${PROJECT_SOURCE_DIR}/.clang-tidy"
)

if(NOT EXISTS "${CPPL_CLANG_TIDY_CONFIG}")
    message(
        FATAL_ERROR
        "C++L requires a repository .clang-tidy file:\n"
        "  ${CPPL_CLANG_TIDY_CONFIG}"
    )
endif()

if(NOT CMAKE_EXPORT_COMPILE_COMMANDS)
    message(
        FATAL_ERROR
        "clang-tidy requires CMAKE_EXPORT_COMPILE_COMMANDS=ON."
    )
endif()

# -----------------------------------------------------------------------------
# Translation-unit discovery
# -----------------------------------------------------------------------------
#
# clang-tidy should be invoked on translation units rather than standalone
# headers. Headers included by these translation units are analyzed as part of
# normal compilation.

file(
    GLOB_RECURSE CPPL_LINT_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false

    "${PROJECT_SOURCE_DIR}/*.c"
    "${PROJECT_SOURCE_DIR}/*.cc"
    "${PROJECT_SOURCE_DIR}/*.cpp"
    "${PROJECT_SOURCE_DIR}/*.cxx"
)

# Never lint generated or externally-owned code.
list(
    FILTER CPPL_LINT_FILES
    EXCLUDE REGEX
    "/build/"
)

list(
    FILTER CPPL_LINT_FILES
    EXCLUDE REGEX
    "/third_party/"
)

list(
    FILTER CPPL_LINT_FILES
    EXCLUDE REGEX
    "/external/"
)

list(
    FILTER CPPL_LINT_FILES
    EXCLUDE REGEX
    "/vendor/"
)

list(
    FILTER CPPL_LINT_FILES
    EXCLUDE REGEX
    "/generated/"
)

list(
    SORT CPPL_LINT_FILES
)

# -----------------------------------------------------------------------------
# Target
# -----------------------------------------------------------------------------

if(CPPL_LINT_FILES)

    add_custom_target(
        lint

        COMMAND
            "${CPPL_CLANG_TIDY}"
            -p
            "${CMAKE_BINARY_DIR}"
            --config-file="${CPPL_CLANG_TIDY_CONFIG}"
            ${CPPL_LINT_FILES}

        WORKING_DIRECTORY
            "${PROJECT_SOURCE_DIR}"

        COMMENT
            "Running clang-tidy on C++L sources"

        VERBATIM
        COMMAND_EXPAND_LISTS
    )

else()

    add_custom_target(
        lint
        COMMAND
            "${CMAKE_COMMAND}" -E echo
            "No C/C++ translation units found to lint."
        VERBATIM
    )

endif()