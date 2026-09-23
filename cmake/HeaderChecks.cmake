# cmake/HeaderChecks.cmake
#
# Every header is analyzed as a translation unit of its own.
#
# clang-tidy reports include hygiene only for the main file it analyzes, and a
# header reached through the units that include it is never one. So a header
# could use a name it does not include, relying on whatever its includers had
# already included, and lint would never notice (AGENTS.md 36).
#
#   cppl_check_headers(<target> [<directory>...])
#       Declares the headers under each directory (default: the calling
#       directory) as the target's own.
#
#   cppl_add_header_checks(<directory>...)
#       Fails the configuration if a header under those directories belongs to
#       no target, or to two, so a header added without an owner cannot fall
#       out of the check unnoticed. Then gives each target's headers an object
#       library that is never built but is compiled, in the compilation
#       database, with exactly the target's include paths, definitions and
#       dependencies. `lint` therefore analyzes each header as a main file: it
#       must compile from its own includes alone, and it must directly include
#       what it uses.
#
# The object libraries live in a directory of their own. Compiling a header
# takes marking it as C++, a property of the file within one directory, and
# a component that lists its headers among its own sources must not start
# compiling them.

include_guard(GLOBAL)

function(cppl_check_headers target)
    set(roots ${ARGN})
    if(NOT roots)
        set(roots ".")
    endif()

    set(headers)
    foreach(root IN LISTS roots)
        cmake_path(
            APPEND CMAKE_CURRENT_SOURCE_DIR "${root}"
            OUTPUT_VARIABLE directory
        )
        cmake_path(NORMAL_PATH directory)
        # Normalizing `.` leaves a trailing separator, which the glob would
        # double into every path it returns.
        string(REGEX REPLACE "/+$" "" directory "${directory}")
        file(
            GLOB_RECURSE found
            CONFIGURE_DEPENDS
            "${directory}/*.hpp"
        )
        list(APPEND headers ${found})
    endforeach()

    if(NOT headers)
        message(FATAL_ERROR "cppl_check_headers(${target}) found no headers")
    endif()

    set_property(GLOBAL APPEND PROPERTY CPPL_CHECKED_HEADERS ${headers})
    set_property(GLOBAL APPEND PROPERTY CPPL_HEADER_CHECK_TARGETS ${target})
    set_property(GLOBAL PROPERTY "CPPL_HEADERS_${target}" ${headers})
endfunction()

function(cppl_add_header_checks)
    get_property(checked GLOBAL PROPERTY CPPL_CHECKED_HEADERS)

    set(unique ${checked})
    list(REMOVE_DUPLICATES unique)
    list(LENGTH checked checked_count)
    list(LENGTH unique unique_count)
    if(NOT checked_count EQUAL unique_count)
        message(FATAL_ERROR "a header is declared by cppl_check_headers for two targets")
    endif()

    foreach(directory IN LISTS ARGN)
        file(
            GLOB_RECURSE present
            CONFIGURE_DEPENDS
            "${PROJECT_SOURCE_DIR}/${directory}/*.hpp"
        )
        foreach(header IN LISTS present)
            if(NOT header IN_LIST checked)
                message(
                    FATAL_ERROR
                    "${header} belongs to no target's cppl_check_headers, so lint would never analyze it on its own"
                )
            endif()
        endforeach()
    endforeach()

    add_subdirectory(
        "${PROJECT_SOURCE_DIR}/cmake/header-checks"
        "${CMAKE_BINARY_DIR}/header-checks"
    )
endfunction()
