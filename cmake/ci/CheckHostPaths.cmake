# cmake/ci/CheckHostPaths.cmake
#
# Refuse host-machine absolute paths committed to shared configuration.
#
# Run standalone:
#
#   cmake -P cmake/ci/CheckHostPaths.cmake
#
# The class of defect this exists for:
#
#   ld: warning: search path '/opt/homebrew/opt/wxwidgets/lib' not found
#
# A path like that works on the machine that committed it and nowhere else.
# Toolchain locations are legitimate -- they must be *discovered* (brew --prefix,
# llvm-config, find_program) or supplied by the selected environment, never
# written into a file that every machine reads.
#
# This check is deliberately narrow. It scans the files that configure the
# build for the host-specific prefixes that have actually caused breakage,
# rather than trying to ban every absolute path.

cmake_minimum_required(VERSION 3.25)

get_filename_component(CPPL_CI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# Files that every machine reads and must therefore stay host-neutral.
set(CPPL_SCANNED_GLOBS
    "CMakeLists.txt"
    "CMakePresets.json"
    "Makefile"
    "cmake/*.cmake"
    "cmake/ci/*.cmake"
    "*/CMakeLists.txt"
    "*/*/CMakeLists.txt"
    "tools/ci/*.sh"
    "tools/ci/*.ps1"
    "docker/ci/*/Dockerfile"
    ".github/workflows/*.yml"
)

# Prefixes that name one particular machine's filesystem layout.
set(CPPL_FORBIDDEN_PATTERNS
    "/opt/homebrew/"
    "/usr/local/opt/"
    "/usr/local/Cellar/"
    "/Users/"
    "/home/[a-z]"
    "C:[/\\\\]Users[/\\\\]"
)

set(CPPL_SCANNED_FILES "")

foreach(glob IN LISTS CPPL_SCANNED_GLOBS)
    file(GLOB matched LIST_DIRECTORIES false "${CPPL_CI_ROOT}/${glob}")
    list(APPEND CPPL_SCANNED_FILES ${matched})
endforeach()

list(REMOVE_DUPLICATES CPPL_SCANNED_FILES)
list(SORT CPPL_SCANNED_FILES)

# This file states the forbidden prefixes literally, so it cannot scan itself.
list(REMOVE_ITEM CPPL_SCANNED_FILES "${CMAKE_CURRENT_LIST_FILE}")

set(CPPL_VIOLATIONS "")
set(CPPL_SCANNED_COUNT 0)

foreach(file IN LISTS CPPL_SCANNED_FILES)
    if(IS_DIRECTORY "${file}")
        continue()
    endif()

    math(EXPR CPPL_SCANNED_COUNT "${CPPL_SCANNED_COUNT} + 1")

    file(STRINGS "${file}" lines ENCODING UTF-8)
    file(RELATIVE_PATH relative "${CPPL_CI_ROOT}" "${file}")

    set(number 0)

    foreach(line IN LISTS lines)
        math(EXPR number "${number} + 1")

        # A comment may legitimately name a path as an example. The rule is
        # about configuration the build consumes, not prose explaining it.
        if(line MATCHES "^[ \t]*#")
            continue()
        endif()

        foreach(pattern IN LISTS CPPL_FORBIDDEN_PATTERNS)
            if(line MATCHES "${pattern}")
                string(STRIP "${line}" stripped)
                list(APPEND CPPL_VIOLATIONS "  ${relative}:${number}\n      ${stripped}")
                break()
            endif()
        endforeach()
    endforeach()
endforeach()

if(CPPL_VIOLATIONS)
    list(LENGTH CPPL_VIOLATIONS count)
    string(REPLACE ";" "\n" report "${CPPL_VIOLATIONS}")

    message(FATAL_ERROR
        "\n"
        "Host-specific absolute paths in shared configuration (${count}):\n"
        "\n"
        "${report}\n"
        "\n"
        "These paths describe one machine. Discover them instead:\n"
        "\n"
        "  brew --prefix <formula>      Homebrew packages\n"
        "  llvm-config --prefix         LLVM installations\n"
        "  find_program / find_package  everything else\n"
        "\n"
        "An environment may supply a path (LLVM_ROOT); the repository may not\n"
        "assume one.\n")
endif()

message(STATUS "Host-path check: ${CPPL_SCANNED_COUNT} files clean.")
