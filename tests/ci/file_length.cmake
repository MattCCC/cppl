# The file-length check (cmake/ci/CheckFileLength.cmake), run over trees built
# here for it.
#
#   cmake -DCHECK=<CheckFileLength.cmake> -DWORK=<work directory> -P file_length.cmake
#
# The repository itself only shows that today's files pass. These trees show
# what the check refuses: a file one line over the limit, under each C++
# extension and at any depth, with or without a final newline; and what the
# transitional list allows and refuses. They also show what it must not count:
# a file at the limit, a file that is not C++, and the directories that hold no
# source, while a tree that itself lives under such a directory is still
# checked.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED CHECK OR NOT DEFINED WORK)
    message(FATAL_ERROR "file_length.cmake needs -DCHECK=<check script> -DWORK=<work directory>")
endif()

set(run "${WORK}/file-length")
file(REMOVE_RECURSE "${run}")
file(MAKE_DIRECTORY "${run}")

set(failures "")
set(trees 0)

# A file of `count` lines, each ended by a newline; with `unterminated`, the
# last one is not.
function(lines path count)
    cmake_parse_arguments(PARSE_ARGV 2 arg "UNTERMINATED" "" "")
    string(REPEAT "// a line\n" ${count} text)
    if(arg_UNTERMINATED)
        string(REGEX REPLACE "\n$" "" text "${text}")
    endif()
    get_filename_component(directory "${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${directory}")
    file(WRITE "${path}" "${text}")
endfunction()

function(fresh out_var)
    math(EXPR next "${trees} + 1")
    set(trees ${next} PARENT_SCOPE)
    set(${out_var} "${run}/tree-${next}" PARENT_SCOPE)
    file(MAKE_DIRECTORY "${run}/tree-${next}")
endfunction()

# Runs the check over `root`, with the list `oversized` when one is named, and
# expects it to pass, or to fail naming each of `expected`.
function(expect what root outcome)
    cmake_parse_arguments(PARSE_ARGV 3 arg "" "OVERSIZED" "MENTIONS")
    set(arguments "-DCPPL_FILELENGTH_ROOT=${root}")
    if(arg_OVERSIZED)
        list(APPEND arguments "-DCPPL_FILELENGTH_OVERSIZED=${arg_OVERSIZED}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" ${arguments} -P "${CHECK}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output)
    if(outcome STREQUAL "PASS" AND NOT result EQUAL 0)
        string(APPEND failures "  ${what}: refused, and should pass\n${output}\n")
    elseif(outcome STREQUAL "FAIL" AND result EQUAL 0)
        string(APPEND failures "  ${what}: passed, and should be refused\n${output}\n")
    endif()
    string(REGEX REPLACE "[ \t\r\n]+" " " flattened "${output}")
    foreach(mention IN LISTS arg_MENTIONS)
        string(FIND "${flattened}" "${mention}" at)
        if(at EQUAL -1)
            string(APPEND failures "  ${what}: the report does not say `${mention}`\n${output}\n")
        endif()
    endforeach()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# At the limit, and not C++, and in a directory that holds no source: nothing
# is refused.
fresh(clean)
lines("${clean}/at_limit.cpp" 1000)
lines("${clean}/compiler/src/unterminated_at_limit.cpp" 1000 UNTERMINATED)
lines("${clean}/docs/SPEC.md" 5000)
lines("${clean}/notes.txt" 5000)
foreach(skipped IN ITEMS build/dev tmp .git .claude node_modules editors/vscode/node_modules build-asan Testing .cache)
    lines("${clean}/${skipped}/generated.cpp" 2000)
endforeach()
expect("files at the limit, files that are not C++ and skipped directories" "${clean}" PASS)

# One line over, under every C++ extension.
foreach(extension IN ITEMS cpp hpp h cc cxx hxx ipp inl tpp)
    fresh(tree)
    lines("${tree}/at_limit.cpp" 1000)
    lines("${tree}/tests/over.${extension}" 1001)
    expect("a .${extension} file of 1001 lines" "${tree}" FAIL MENTIONS "tests/over.${extension}: 1001 lines")
endforeach()

# A final line without a newline is a line.
fresh(tree)
lines("${tree}/over.cpp" 1001 UNTERMINATED)
expect("1001 lines, the last unterminated" "${tree}" FAIL MENTIONS "over.cpp: 1001 lines")

# At any depth, at the top of the tree, and with every offender named.
fresh(tree)
lines("${tree}/compiler/a/b/c/deep.cpp" 1200)
lines("${tree}/top.hpp" 1001)
expect("files deep in the tree and at its top" "${tree}" FAIL
       MENTIONS "compiler/a/b/c/deep.cpp: 1200 lines" "top.hpp: 1001 lines")

# A tree that lives under a directory named like a skipped one is checked.
set(nested "${run}/build/tmp/checkout")
lines("${nested}/src/over.cpp" 1001)
expect("a tree under build/tmp" "${nested}" FAIL MENTIONS "src/over.cpp: 1001 lines")

# The transitional list: a listed file may stay at its recorded count, and no
# more; once at or below the limit it must leave the list; the list names only
# files that exist, each once, in its one form.
fresh(tree)
lines("${tree}/listed.cpp" 1500)
set(list "${run}/oversized-${trees}.txt")
file(WRITE "${list}" "# a comment\n\nlisted.cpp 1500\n")
expect("a listed file at its recorded count" "${tree}" PASS OVERSIZED "${list}")
lines("${tree}/listed.cpp" 1400)
expect("a listed file below its recorded count" "${tree}" PASS OVERSIZED "${list}")
lines("${tree}/listed.cpp" 1501)
expect("a listed file grown" "${tree}" FAIL OVERSIZED "${list}"
       MENTIONS "listed.cpp: 1501 lines, grown beyond the 1500")
lines("${tree}/listed.cpp" 1000)
expect("a listed file at the limit" "${tree}" FAIL OVERSIZED "${list}"
       MENTIONS "listed.cpp: 1000 lines, within the limit now")
lines("${tree}/listed.cpp" 1500)
lines("${tree}/unlisted.cpp" 1001)
expect("an unlisted file beside a listed one" "${tree}" FAIL OVERSIZED "${list}"
       MENTIONS "unlisted.cpp: 1001 lines")
expect("a tree named without its list" "${tree}" FAIL MENTIONS "listed.cpp: 1500 lines")

fresh(tree)
lines("${tree}/listed.cpp" 1500)
set(list "${run}/oversized-${trees}.txt")
file(WRITE "${list}" "listed.cpp 1500\ngone.cpp 2000\n")
expect("a listed file that does not exist" "${tree}" FAIL OVERSIZED "${list}"
       MENTIONS "gone.cpp is listed in oversized.txt but does not exist")
file(WRITE "${list}" "listed.cpp 1500\nlisted.cpp 1500\n")
expect("a file listed twice" "${tree}" FAIL OVERSIZED "${list}" MENTIONS "lists listed.cpp twice")
file(WRITE "${list}" "listed.cpp\n")
expect("an entry without a count" "${tree}" FAIL OVERSIZED "${list}"
       MENTIONS "`listed.cpp` is not `<path> <line count>`")
lines("${tree}/notes.md" 1500)
file(WRITE "${list}" "listed.cpp 1500\nnotes.md 1500\n")
expect("a listed file that is not C++" "${tree}" FAIL OVERSIZED "${list}"
       MENTIONS "notes.md is listed in oversized.txt but is not a C++ file this check counts")

if(failures)
    message(FATAL_ERROR "The file-length check decided wrongly:\n${failures}")
endif()
message(STATUS "File-length check: ${trees} trees decided as expected.")
