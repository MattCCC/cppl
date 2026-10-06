# cmake/ci/CheckFrozenDocuments.cmake
#
# Hold a released version's frozen documents to what was released.
#
# Run standalone:
#
#   cmake -P cmake/ci/CheckFrozenDocuments.cmake
#
# From version 1.0.0 on, docs/SPEC.md, docs/GRAMMAR.md and docs/KERNEL.md are
# frozen at the version CMakeLists.txt names (docs/ROADMAP.md, gate G17). A
# freeze that is only a sentence would survive any edit, so it is checked:
#
#   1. Each frozen document states `**Frozen:** C++L <version>`.
#
#   2. Each one's SHA-256, over its text with CRLF line endings read as LF so a
#      Windows checkout agrees, is the digest docs/STATUS.md records for it in
#      "V1 closure: stability". Changing a frozen document therefore means
#      changing the recorded digest in the same commit, which a reviewer sees,
#      and which docs/STATUS.md says needs an RFC and a new version.
#
#   3. docs/STATUS.md's header says the language specification and the proof
#      system are frozen at that version.
#
#   4. The core and kernel versions docs/KERNEL.md names are the ones
#      kernel/include/cppl/kernel/version.hpp defines.
#
# A version below 1.0.0 freezes nothing, and nothing is checked.

cmake_minimum_required(VERSION 3.25)

get_filename_component(CPPL_CI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

function(cppl_read_normalized path out_var)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${path} not found")
    endif()
    file(READ "${path}" text)
    string(REPLACE "\r\n" "\n" text "${text}")
    set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

cppl_read_normalized("${CPPL_CI_ROOT}/CMakeLists.txt" project_text)
string(REGEX MATCH "project\\([^)]*VERSION[ \t\n]+([0-9]+)\\.([0-9]+)\\.([0-9]+)" matched "${project_text}")
if(NOT matched)
    message(FATAL_ERROR "CMakeLists.txt names no project VERSION")
endif()
set(version "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
if(CMAKE_MATCH_1 LESS 1)
    message(STATUS "Frozen documents: version ${version} freezes nothing.")
    return()
endif()

set(problems "")
cppl_read_normalized("${CPPL_CI_ROOT}/docs/STATUS.md" status_text)

foreach(document IN ITEMS SPEC GRAMMAR KERNEL)
    set(relative "docs/${document}.md")
    cppl_read_normalized("${CPPL_CI_ROOT}/${relative}" text)

    string(FIND "${text}" "**Frozen:** C++L ${version}" stated)
    if(stated EQUAL -1)
        string(APPEND problems "  ${relative} does not state `**Frozen:** C++L ${version}`\n")
    endif()

    string(SHA256 digest "${text}")
    string(REGEX MATCH "\n\\| `${relative}` \\| `([0-9a-f]+)` \\|" recorded "${status_text}")
    if(NOT recorded)
        string(APPEND problems "  docs/STATUS.md records no digest for ${relative}\n")
    elseif(NOT CMAKE_MATCH_1 STREQUAL digest)
        string(APPEND problems
               "  ${relative} has SHA-256 ${digest}, and docs/STATUS.md records ${CMAKE_MATCH_1} for the frozen text\n")
    endif()
endforeach()

foreach(field IN ITEMS "Language specification frozen" "Proof system frozen")
    string(FIND "${status_text}" "**${field}:** Yes, at C++L ${version}" stated)
    if(stated EQUAL -1)
        string(APPEND problems "  docs/STATUS.md does not say `**${field}:** Yes, at C++L ${version}`\n")
    endif()
endforeach()

cppl_read_normalized("${CPPL_CI_ROOT}/kernel/include/cppl/kernel/version.hpp" kernel_versions)
cppl_read_normalized("${CPPL_CI_ROOT}/docs/KERNEL.md" kernel_reference)
foreach(constant IN ITEMS kKernelVersion kFormalCoreVersion)
    string(REGEX MATCH "${constant} = \"([^\"]+)\"" defined "${kernel_versions}")
    if(NOT defined)
        string(APPEND problems "  kernel/include/cppl/kernel/version.hpp defines no ${constant}\n")
        continue()
    endif()
    string(FIND "${kernel_reference}" "`${CMAKE_MATCH_1}`" named)
    if(named EQUAL -1)
        string(APPEND problems "  docs/KERNEL.md does not name `${CMAKE_MATCH_1}`, the ${constant} the kernel defines\n")
    endif()
endforeach()

if(problems)
    message(FATAL_ERROR "Frozen documents of C++L ${version} differ from what was frozen:\n${problems}")
endif()
message(STATUS "Frozen documents: SPEC.md, GRAMMAR.md and KERNEL.md are the text frozen at C++L ${version}.")
