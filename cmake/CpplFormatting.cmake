# cmake/CpplFormatting.cmake
#
# C++L-specific formatting: canonical placement of `expects`/`ensures`/
# `invariant`/`proves` clauses (Formatting.cmake's clang-format pass handles
# ordinary C/C++ layout but knows nothing about C++L syntax).
#
# This runs over the C++L source fixtures under tests/fixtures, using the
# `cppl-format` tool built from compiler/formatter (the same engine cppl-lsp
# uses for textDocument/formatting), and is folded into the repository's
# existing `format`/`format-check` targets so CI exercises exactly one
# command for both layers.

include_guard(GLOBAL)

if(NOT TARGET format OR NOT TARGET format-check)
    message(
        FATAL_ERROR
        "CpplFormatting.cmake requires Formatting.cmake to be included first."
    )
endif()

file(
    GLOB_RECURSE CPPL_L_FORMAT_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false

    "${PROJECT_SOURCE_DIR}/tests/fixtures/*.cpp"
)

list(
    SORT CPPL_L_FORMAT_FILES
)

if(CPPL_L_FORMAT_FILES)

    add_custom_target(
        cppl-format-apply

        COMMAND
            "$<TARGET_FILE:cppl-format>"
            -i
            ${CPPL_L_FORMAT_FILES}

        WORKING_DIRECTORY
            "${PROJECT_SOURCE_DIR}"

        COMMENT
            "Formatting C++L sources"

        VERBATIM
        COMMAND_EXPAND_LISTS
    )

    add_dependencies(cppl-format-apply cppl-format)
    add_dependencies(format cppl-format-apply)

    add_custom_target(
        cppl-format-check

        COMMAND
            "$<TARGET_FILE:cppl-format>"
            --check
            ${CPPL_L_FORMAT_FILES}

        COMMAND
            "${CMAKE_COMMAND}" -E echo "success"

        WORKING_DIRECTORY
            "${PROJECT_SOURCE_DIR}"

        COMMENT
            "Checking C++L source formatting"

        VERBATIM
        COMMAND_EXPAND_LISTS
    )

    add_dependencies(cppl-format-check cppl-format)
    add_dependencies(format-check cppl-format-check)

endif()
