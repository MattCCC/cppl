# cmake/Formatting.cmake
#
# Repository-wide C/C++ formatting targets.
#
# Provides:
#
#   format
#       Reformat all project C/C++ source and header files in-place.
#
#   format-check
#       Verify formatting without modifying files.
#
# clang-format is resolved by LLVMToolchain.cmake and must come from the same
# LLVM installation as the Clang driver selected by C++L.

include_guard(GLOBAL)

if(NOT COMMAND cppl_find_llvm_tool)
    message(
        FATAL_ERROR
        "Formatting.cmake requires LLVMToolchain.cmake to be included first."
    )
endif()

cppl_find_llvm_tool(
    CPPL_CLANG_FORMAT
    clang-format
)

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------

set(
    CPPL_CLANG_FORMAT_CONFIG
    "${PROJECT_SOURCE_DIR}/.clang-format"
)

if(NOT EXISTS "${CPPL_CLANG_FORMAT_CONFIG}")
    message(
        FATAL_ERROR
        "C++L requires a repository .clang-format file:\n"
        "  ${CPPL_CLANG_FORMAT_CONFIG}"
    )
endif()

# -----------------------------------------------------------------------------
# Source discovery
# -----------------------------------------------------------------------------

file(
    GLOB_RECURSE CPPL_FORMAT_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false

    "${PROJECT_SOURCE_DIR}/*.c"
    "${PROJECT_SOURCE_DIR}/*.cc"
    "${PROJECT_SOURCE_DIR}/*.cpp"
    "${PROJECT_SOURCE_DIR}/*.cxx"

    "${PROJECT_SOURCE_DIR}/*.h"
    "${PROJECT_SOURCE_DIR}/*.hh"
    "${PROJECT_SOURCE_DIR}/*.hpp"
    "${PROJECT_SOURCE_DIR}/*.hxx"

    "${PROJECT_SOURCE_DIR}/*.inc"
    "${PROJECT_SOURCE_DIR}/*.ipp"
    "${PROJECT_SOURCE_DIR}/*.tpp"
)

# Never format generated or externally-owned code.
list(
    FILTER CPPL_FORMAT_FILES
    EXCLUDE REGEX
    "/build/"
)

list(
    FILTER CPPL_FORMAT_FILES
    EXCLUDE REGEX
    "/third_party/"
)

list(
    FILTER CPPL_FORMAT_FILES
    EXCLUDE REGEX
    "/external/"
)

list(
    FILTER CPPL_FORMAT_FILES
    EXCLUDE REGEX
    "/vendor/"
)

list(
    FILTER CPPL_FORMAT_FILES
    EXCLUDE REGEX
    "/generated/"
)

# `tmp/` is ignored scratch. It holds C++L sources and emitted runtime text,
# neither of which is C++ this project's style applies to.
list(
    FILTER CPPL_FORMAT_FILES
    EXCLUDE REGEX
    "/tmp/"
)

list(
    SORT CPPL_FORMAT_FILES
)

# -----------------------------------------------------------------------------
# Targets
# -----------------------------------------------------------------------------

if(CPPL_FORMAT_FILES)

    add_custom_target(
        format

        COMMAND
            "${CPPL_CLANG_FORMAT}"
            -i
            -style=file
            ${CPPL_FORMAT_FILES}

        WORKING_DIRECTORY
            "${PROJECT_SOURCE_DIR}"

        COMMENT
            "Formatting C++L C/C++ sources"

        VERBATIM
        COMMAND_EXPAND_LISTS
    )

    add_custom_target(
        format-check

        COMMAND
            "${CPPL_CLANG_FORMAT}"
            --dry-run
            --Werror
            -style=file
            ${CPPL_FORMAT_FILES}

        WORKING_DIRECTORY
            "${PROJECT_SOURCE_DIR}"

        COMMENT
            "Checking C++L C/C++ source formatting"

        VERBATIM
        COMMAND_EXPAND_LISTS
    )

else()

    add_custom_target(
        format
        COMMAND
            "${CMAKE_COMMAND}" -E echo
            "No C/C++ files found to format."
        VERBATIM
    )

    add_custom_target(
        format-check
        COMMAND
            "${CMAKE_COMMAND}" -E echo
            "No C/C++ files found to check."
        VERBATIM
    )

endif()