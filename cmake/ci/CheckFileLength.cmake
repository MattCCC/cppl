# cmake/ci/CheckFileLength.cmake
#
# Hold every C++ source and header in the repository to at most 1000 lines.
#
# Run standalone:
#
#   cmake -P cmake/ci/CheckFileLength.cmake
#
# A file that long has stopped having one responsibility, and a reviewer can no
# longer hold it in mind; a proof-relevant change hidden in it is the change
# nobody reads. AGENTS.md 36 therefore limits every C++ file -- compiler, Clang
# bridge, kernel, language server, tools, tests, test support and fixtures -- to
# 1000 lines, and a file is split by responsibility before it would cross the
# limit, never by an arbitrary cut. Markdown, the specification and every other
# file that is not C++ are not counted.
#
# Every file with a C++ extension under the source tree is counted, wherever it
# lives. Only directories that hold no source are skipped: build trees, scratch
# space, version control and tool state, and installed dependencies. They are
# matched against paths relative to the tree, so a checkout that itself lives
# under a directory named `build` or `tmp` is still checked.
#
# A line is what an editor numbers: each newline ends one, and text after the
# last newline is one more.
#
# No file is exempt.
#
# For the check's own tests, a tree may be named instead:
#
#   cmake -DCPPL_FILELENGTH_ROOT=<directory> -P cmake/ci/CheckFileLength.cmake

cmake_minimum_required(VERSION 3.25)

set(CPPL_FILELENGTH_LIMIT 1000)

if(DEFINED CPPL_FILELENGTH_ROOT)
    get_filename_component(CPPL_CI_ROOT "${CPPL_FILELENGTH_ROOT}" ABSOLUTE)
else()
    get_filename_component(CPPL_CI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

if(NOT IS_DIRECTORY "${CPPL_CI_ROOT}")
    message(FATAL_ERROR "${CPPL_CI_ROOT} is not a directory")
endif()

set(CPPL_CXX_PATTERNS *.cpp *.hpp *.h *.cc *.cxx *.hxx *.ipp *.inl *.tpp)

# A directory with one of these names, at any depth, holds no source.
set(CPPL_SKIPPED_DIRECTORIES
    .git
    .claude
    .cache
    .code-review-graph
    .gradle
    node_modules
    vcpkg_installed
    CMakeFiles
    Testing
    build
    Build
    tmp
    bin
    obj
    dist)

function(cppl_skipped relative out_var)
    string(REPLACE "/" ";" components "${relative}")
    list(POP_BACK components)
    foreach(component IN LISTS components)
        if(component IN_LIST CPPL_SKIPPED_DIRECTORIES OR component MATCHES "^build-")
            set(${out_var} TRUE PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} FALSE PARENT_SCOPE)
endfunction()

function(cppl_line_count path out_var)
    file(READ "${path}" text)
    string(LENGTH "${text}" length)
    string(REPLACE "\n" "" joined "${text}")
    string(LENGTH "${joined}" joined_length)
    math(EXPR count "${length} - ${joined_length}")
    if(length GREATER 0)
        math(EXPR last "${length} - 1")
        string(SUBSTRING "${text}" ${last} 1 final)
        if(NOT final STREQUAL "\n")
            math(EXPR count "${count} + 1")
        endif()
    endif()
    set(${out_var} ${count} PARENT_SCOPE)
endfunction()

# The top-level directories are listed first so a skipped one, which may hold a
# whole build tree, is never walked.
file(GLOB top LIST_DIRECTORIES true RELATIVE "${CPPL_CI_ROOT}" "${CPPL_CI_ROOT}/*")
set(files "")
foreach(entry IN LISTS top)
    if(IS_DIRECTORY "${CPPL_CI_ROOT}/${entry}")
        cppl_skipped("${entry}/x" skipped)
        if(skipped)
            continue()
        endif()
        foreach(pattern IN LISTS CPPL_CXX_PATTERNS)
            file(GLOB_RECURSE found LIST_DIRECTORIES false RELATIVE "${CPPL_CI_ROOT}"
                 "${CPPL_CI_ROOT}/${entry}/${pattern}")
            list(APPEND files ${found})
        endforeach()
    elseif(entry MATCHES "\\.(cpp|hpp|h|cc|cxx|hxx|ipp|inl|tpp)$")
        list(APPEND files "${entry}")
    endif()
endforeach()
list(REMOVE_DUPLICATES files)
list(SORT files)

set(problems "")
set(checked 0)
foreach(relative IN LISTS files)
    cppl_skipped("${relative}" skipped)
    if(skipped OR IS_DIRECTORY "${CPPL_CI_ROOT}/${relative}")
        continue()
    endif()
    math(EXPR checked "${checked} + 1")
    cppl_line_count("${CPPL_CI_ROOT}/${relative}" lines)
    if(lines GREATER CPPL_FILELENGTH_LIMIT)
        string(APPEND problems "  ${relative}: ${lines} lines\n")
    endif()
endforeach()

if(problems)
    message(FATAL_ERROR
        "\n"
        "C++ files over the ${CPPL_FILELENGTH_LIMIT}-line limit:\n"
        "\n"
        "${problems}"
        "\n"
        "Split a file by responsibility before it crosses the limit, into files\n"
        "named for what they hold (AGENTS.md 36).\n")
endif()

message(STATUS "File length: ${checked} C++ files checked against ${CPPL_FILELENGTH_LIMIT} lines.")
