# cmake/Linting.cmake
#
# Repository-wide clang-tidy integration.
#
# Provides:
#
#   lint
#       Run clang-tidy over all project C/C++ translation units and headers.
#
#   tidy
#       Run clang-tidy with automatic fixes over all project C/C++
#       translation units and headers.
#
#   lint-changed
#       Run clang-tidy over only the C/C++ translation units and headers that
#       differ from a base git ref (BASE_REF, default: merge-base with main).
#       Intended for routine local development; CI still uses `lint`.
#
#   tidy-changed
#       Same restriction as lint-changed, with automatic fixes.
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

cppl_find_llvm_tool(
    CPPL_CLANG_APPLY_REPLACEMENTS
    clang-apply-replacements
)

# run-clang-tidy drives clang-tidy across the compilation database in parallel.
# Analyzing the tree one translation unit at a time is serial by construction
# and far too slow to run routinely.
#
# It is a Python script rather than an LLVM binary, so it answers no --version
# probe and cannot come from cppl_find_llvm_tool. It is taken directly from the
# tools directory that already resolved clang-tidy, which keeps the driver and
# the tool it drives from the same installation.

set(
    CPPL_RUN_CLANG_TIDY
    "${CPPL_LLVM_TOOLS_DIR}/run-clang-tidy"
)

if(NOT EXISTS "${CPPL_RUN_CLANG_TIDY}")
    message(
        FATAL_ERROR
        "C++L requires run-clang-tidy from the selected LLVM installation:\n"
        "  ${CPPL_RUN_CLANG_TIDY}"
    )
endif()

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

# The configuration is deliberately not passed with --config-file. That flag
# replaces clang-tidy's directory walk, which would silently discard the
# per-directory .clang-tidy overrides (such as tests/.clang-tidy). Discovery
# from each translation unit's own directory is what makes those apply.

if(NOT CMAKE_EXPORT_COMPILE_COMMANDS)
    message(
        FATAL_ERROR
        "clang-tidy requires CMAKE_EXPORT_COMPILE_COMMANDS=ON."
    )
endif()

# The compile database records the host compiler's invocation, which on Apple
# platforms omits the SDK path because Apple Clang infers it. LLVM's clang-tidy
# does not, so the sysroot must be restored explicitly or every standard header
# fails to resolve.

set(CPPL_CLANG_TIDY_EXTRA_ARGS)

if(APPLE)
    set(CPPL_CLANG_TIDY_SYSROOT "${CMAKE_OSX_SYSROOT}")

    if(NOT CPPL_CLANG_TIDY_SYSROOT)
        execute_process(
            COMMAND xcrun --show-sdk-path
            OUTPUT_VARIABLE CPPL_CLANG_TIDY_SYSROOT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()

    if(NOT CPPL_CLANG_TIDY_SYSROOT)
        message(
            FATAL_ERROR
            "clang-tidy requires a macOS SDK path; none was found via "
            "CMAKE_OSX_SYSROOT or 'xcrun --show-sdk-path'."
        )
    endif()

    list(
        APPEND CPPL_CLANG_TIDY_EXTRA_ARGS
        "-extra-arg=-isysroot"
        "-extra-arg=${CPPL_CLANG_TIDY_SYSROOT}"
    )
endif()

# One clang-tidy process per hardware thread; the run is CPU-bound and the
# translation units are independent.

include(ProcessorCount)
ProcessorCount(CPPL_LINT_JOBS)

if(CPPL_LINT_JOBS EQUAL 0)
    set(CPPL_LINT_JOBS 1)
endif()

# -----------------------------------------------------------------------------
# Translation-unit discovery
# -----------------------------------------------------------------------------
#
# The compilation database is the set of translation units, so run-clang-tidy
# needs no file list of its own. It is also the correct set: generated,
# fixture, and externally-owned sources are not build targets and so never
# appear there. Every project header is also an entry of its own, analyzed as a
# main file with its owning target's flags (cmake/HeaderChecks.cmake), so
# misc-include-cleaner holds a header to the includes it states rather than to
# whatever its includers happened to include first.
#
# Files are deliberately not passed positionally. run-clang-tidy treats each
# argument as a Python regex over the path, and the repository directory name
# ("c++l") is not a valid one.

# -----------------------------------------------------------------------------
# Targets
# -----------------------------------------------------------------------------

# -hide-progress suppresses run-clang-tidy's per-file "[n/m] <full clang-tidy
# command line>" lines, which otherwise print the entire invocation (binary
# path, sysroot args, build path) once per translation unit for no
# diagnostic value.

add_custom_target(
    lint

    COMMAND
        "${CPPL_RUN_CLANG_TIDY}"
        -p
        "${CMAKE_BINARY_DIR}"
        -j
        ${CPPL_LINT_JOBS}
        -quiet
        -hide-progress
        -clang-tidy-binary
        "${CPPL_CLANG_TIDY}"
        -clang-apply-replacements-binary
        "${CPPL_CLANG_APPLY_REPLACEMENTS}"
        ${CPPL_CLANG_TIDY_EXTRA_ARGS}

    COMMAND
        "${CMAKE_COMMAND}" -E echo "success"

    WORKING_DIRECTORY
        "${PROJECT_SOURCE_DIR}"

    COMMENT
        "Running clang-tidy on C++L sources"

    VERBATIM
    COMMAND_EXPAND_LISTS
    USES_TERMINAL
)

add_custom_target(
    tidy

    COMMAND
        "${CPPL_RUN_CLANG_TIDY}"
        -p
        "${CMAKE_BINARY_DIR}"
        -j
        ${CPPL_LINT_JOBS}
        -quiet
        -hide-progress
        -clang-tidy-binary
        "${CPPL_CLANG_TIDY}"
        -clang-apply-replacements-binary
        "${CPPL_CLANG_APPLY_REPLACEMENTS}"
        ${CPPL_CLANG_TIDY_EXTRA_ARGS}
        -fix
        -format

    COMMAND
        "${CMAKE_COMMAND}" -E echo "success"

    WORKING_DIRECTORY
        "${PROJECT_SOURCE_DIR}"

    COMMENT
        "Applying clang-tidy fixes to C++L sources"

    VERBATIM
    COMMAND_EXPAND_LISTS
    USES_TERMINAL
)

# -----------------------------------------------------------------------------
# Targeted (changed-files-only) variants
# -----------------------------------------------------------------------------
#
# `lint`/`tidy` analyze the entire compilation database, which is correct
# for CI but too slow to run routinely during local development. These
# targets restrict clang-tidy to files that differ from a base git ref
# (BASE_REF, default: merge-base with main), driven by scripts/lint-changed.sh
# with the exact same driver, binaries, and extra args as the full targets so
# behavior never drifts between them.

add_custom_target(
    lint-changed

    COMMAND
        "${PROJECT_SOURCE_DIR}/scripts/lint-changed.sh"
        "${CPPL_RUN_CLANG_TIDY}"
        "${CMAKE_BINARY_DIR}"
        "${CPPL_CLANG_TIDY}"
        "${CPPL_CLANG_APPLY_REPLACEMENTS}"
        0
        ${CPPL_LINT_JOBS}
        ${CPPL_CLANG_TIDY_EXTRA_ARGS}

    WORKING_DIRECTORY
        "${PROJECT_SOURCE_DIR}"

    COMMENT
        "Running clang-tidy on changed C++L sources"

    VERBATIM
    COMMAND_EXPAND_LISTS
    USES_TERMINAL
)

add_custom_target(
    tidy-changed

    COMMAND
        "${PROJECT_SOURCE_DIR}/scripts/lint-changed.sh"
        "${CPPL_RUN_CLANG_TIDY}"
        "${CMAKE_BINARY_DIR}"
        "${CPPL_CLANG_TIDY}"
        "${CPPL_CLANG_APPLY_REPLACEMENTS}"
        1
        ${CPPL_LINT_JOBS}
        ${CPPL_CLANG_TIDY_EXTRA_ARGS}

    WORKING_DIRECTORY
        "${PROJECT_SOURCE_DIR}"

    COMMENT
        "Applying clang-tidy fixes to changed C++L sources"

    VERBATIM
    COMMAND_EXPAND_LISTS
    USES_TERMINAL
)