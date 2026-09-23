# cmake/ci/CheckWorkflowSteps.cmake
#
# Hold tools/ci/run-preset.sh to the commands .github/workflows/ci.yml runs.
#
# Run standalone:
#
#   cmake -P cmake/ci/CheckWorkflowSteps.cmake
#
# A local CI run is worth something only if it runs what the workflow runs. The
# workflow's Quality job builds nothing and runs the formatting check and lint;
# a local run that built and tested instead would pass without running either.
#
# Checked:
#
#   1. The Quality job's cmake and ctest commands are the script's ci-quality
#      commands, in order.
#
#   2. Every other job's cmake and ctest commands, with its preset named
#      generically, are the script's commands for any other preset, in order.

cmake_minimum_required(VERSION 3.25)

get_filename_component(CPPL_CI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# A file's lines as a list. Semicolons and brackets would otherwise be read as
# list syntax, so they are replaced first; none of them is compared.
function(cppl_lines path out_var)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${path} not found")
    endif()
    file(READ "${path}" text)
    string(REPLACE ";" "<semicolon>" text "${text}")
    string(REPLACE "[" "<open>" text "${text}")
    string(REPLACE "]" "<close>" text "${text}")
    string(REPLACE "\n" ";" lines "${text}")
    set(${out_var} "${lines}" PARENT_SCOPE)
endfunction()

# The cmake or ctest command a line holds, with any `run:` before it, or empty.
function(cppl_command line out_var)
    # Each pattern needs at least one character: CMake before 3.29 refuses a
    # replacement whose pattern can match nothing.
    string(STRIP "${line}" command)
    string(REGEX REPLACE "^- " "" command "${command}")
    string(REGEX REPLACE "^run:[ \t]*" "" command "${command}")
    string(STRIP "${command}" command)
    if(command MATCHES "^(cmake|ctest) ")
        set(${out_var} "${command}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

# The workflow's commands, job by job.
cppl_lines("${CPPL_CI_ROOT}/.github/workflows/ci.yml" workflow)
set(jobs "")
set(in_jobs FALSE)
set(job "")
foreach(line IN LISTS workflow)
    if(line MATCHES "^jobs:")
        set(in_jobs TRUE)
        continue()
    endif()
    if(NOT in_jobs)
        continue()
    endif()
    if(line MATCHES "^  ([A-Za-z0-9_-]+):[ \t]*$")
        set(job "${CMAKE_MATCH_1}")
        continue()
    endif()
    cppl_command("${line}" command)
    if(job AND command)
        if(NOT job IN_LIST jobs)
            list(APPEND jobs "${job}")
        endif()
        string(REPLACE "\${{ matrix.preset }}" "<preset>" command "${command}")
        list(APPEND "job_${job}" "${command}")
    endif()
endforeach()

if(NOT "quality" IN_LIST jobs)
    message(FATAL_ERROR "ci.yml has no Quality job with cmake commands to compare")
endif()

# The script's commands: the ci-quality branch and the one for any other preset.
cppl_lines("${CPPL_CI_ROOT}/tools/ci/run-preset.sh" script)
set(branch "")
set(script_quality "")
set(script_other "")
foreach(line IN LISTS script)
    string(STRIP "${line}" stripped)
    if(stripped STREQUAL "ci-quality)")
        set(branch quality)
        continue()
    endif()
    if(stripped STREQUAL "*)")
        set(branch other)
        continue()
    endif()
    if(stripped STREQUAL "<semicolon><semicolon>")
        set(branch "")
        continue()
    endif()
    cppl_command("${line}" command)
    if(branch STREQUAL "quality" AND command)
        list(APPEND script_quality "${command}")
    elseif(branch STREQUAL "other" AND command)
        string(REPLACE "\"\${preset}\"" "<preset>" command "${command}")
        list(APPEND script_other "${command}")
    endif()
endforeach()

set(problems "")

function(cppl_compare what expected actual)
    if(NOT "${expected}" STREQUAL "${actual}")
        string(REPLACE ";" "\n    " expected_text "${expected}")
        string(REPLACE ";" "\n    " actual_text "${actual}")
        set(problems
            "${problems}\n  ${what}\n  workflow:\n    ${expected_text}\n  tools/ci/run-preset.sh:\n    ${actual_text}\n"
            PARENT_SCOPE)
    endif()
endfunction()

cppl_compare("the Quality job" "${job_quality}" "${script_quality}")

foreach(job IN LISTS jobs)
    if(job STREQUAL "quality")
        continue()
    endif()
    # A job that names its one preset literally names it generically here.
    set(commands "")
    foreach(command IN LISTS "job_${job}")
        string(REGEX REPLACE "--preset ci-[a-z0-9-]+" "--preset <preset>" command "${command}")
        list(APPEND commands "${command}")
    endforeach()
    cppl_compare("the '${job}' job" "${commands}" "${script_other}")
endforeach()

if(problems)
    message(FATAL_ERROR "tools/ci/run-preset.sh does not run what .github/workflows/ci.yml runs:${problems}")
endif()

list(LENGTH jobs job_count)
message(STATUS "tools/ci/run-preset.sh runs what all ${job_count} workflow jobs with build commands run")
