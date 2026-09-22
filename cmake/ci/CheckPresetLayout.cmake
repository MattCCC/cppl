# cmake/ci/CheckPresetLayout.cmake
#
# Hold CMakePresets.json to the conventions the rest of the CI system relies on.
#
# Run standalone:
#
#   cmake -P cmake/ci/CheckPresetLayout.cmake
#
# Checked:
#
#   1. Every ci-<name> configure preset builds in build/ci/<name>.
#      tools/ci/native.sh derives the directory to erase from the preset name,
#      and two environments sharing a binary directory would mean one
#      compiler reading another compiler's cache.
#
#   2. Every ci-* configure preset has a matching build and test preset, so
#      that `cmake --build --preset X` and `ctest --preset X` work for every
#      X a workflow might name.
#
# This is a convention check, not a schema validator. `cmake --list-presets`
# already rejects a malformed file.

cmake_minimum_required(VERSION 3.25)

get_filename_component(CPPL_CI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CPPL_PRESETS_FILE "${CPPL_CI_ROOT}/CMakePresets.json")

if(NOT EXISTS "${CPPL_PRESETS_FILE}")
    message(FATAL_ERROR "CMakePresets.json not found at ${CPPL_PRESETS_FILE}")
endif()

file(READ "${CPPL_PRESETS_FILE}" presets_json)

set(problems "")

function(cppl_collect_names array_name out_var)
    string(JSON count ERROR_VARIABLE error LENGTH "${presets_json}" "${array_name}")

    if(error)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    set(names "")
    math(EXPR last "${count} - 1")

    foreach(index RANGE 0 ${last})
        string(JSON entry GET "${presets_json}" "${array_name}" ${index})
        string(JSON name GET "${entry}" "name")
        list(APPEND names "${name}")
    endforeach()

    set(${out_var} "${names}" PARENT_SCOPE)
endfunction()

cppl_collect_names(buildPresets build_names)
cppl_collect_names(testPresets test_names)

# -----------------------------------------------------------------------------
# configurePresets
# -----------------------------------------------------------------------------

string(JSON configure_count LENGTH "${presets_json}" configurePresets)
math(EXPR configure_last "${configure_count} - 1")

set(binary_dirs "")
set(seen_dirs "")

foreach(index RANGE 0 ${configure_last})
    string(JSON entry GET "${presets_json}" configurePresets ${index})
    string(JSON name GET "${entry}" "name")

    string(JSON hidden ERROR_VARIABLE hidden_error GET "${entry}" "hidden")

    if(hidden STREQUAL "ON" OR hidden STREQUAL "true")
        continue()
    endif()

    string(JSON binary_dir ERROR_VARIABLE dir_error GET "${entry}" "binaryDir")

    if(dir_error)
        list(APPEND problems "  ${name}: no binaryDir")
        continue()
    endif()

    # Two environments must never share a build tree.
    if("${binary_dir}" IN_LIST seen_dirs)
        list(APPEND problems "  ${name}: binaryDir '${binary_dir}' is already used by another preset")
    endif()

    list(APPEND seen_dirs "${binary_dir}")

    if(NOT name MATCHES "^ci-")
        continue()
    endif()

    string(REGEX REPLACE "^ci-" "" suffix "${name}")
    set(expected "\${sourceDir}/build/ci/${suffix}")

    if(NOT binary_dir STREQUAL expected)
        list(APPEND problems
            "  ${name}: binaryDir is '${binary_dir}', expected '${expected}'")
    endif()

    if(NOT "${name}" IN_LIST build_names)
        list(APPEND problems "  ${name}: no buildPreset of the same name")
    endif()

    if(NOT "${name}" IN_LIST test_names)
        list(APPEND problems "  ${name}: no testPreset of the same name")
    endif()
endforeach()

if(problems)
    string(REPLACE ";" "\n" report "${problems}")

    message(FATAL_ERROR
        "\n"
        "CMakePresets.json breaks the CI preset conventions:\n"
        "\n"
        "${report}\n"
        "\n"
        "Every ci-<name> preset builds in build/ci/<name> and has a build and\n"
        "test preset of the same name, so that a workflow and a developer can\n"
        "name one preset and get the same build.\n")
endif()

message(STATUS "Preset layout: ${configure_count} configure presets consistent.")
