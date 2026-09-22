# cmake/ci/HostEnvironment.cmake
#
# Refuse ambient compiler/linker flags in a CI configuration.
#
# CMake folds several environment variables into the build on the first
# configure: CFLAGS and CXXFLAGS seed CMAKE_<LANG>_FLAGS, and LDFLAGS seeds the
# link flags. A developer shell that exports one of them -- as any Homebrew
# setup guide will tell you to -- silently changes what gets built:
#
#   LDFLAGS=-L/opt/homebrew/opt/wxwidgets/lib
#     -> ld: warning: search path '/opt/homebrew/opt/wxwidgets/lib' not found
#
# That is a build the GitHub runner never performs, which is the whole failure
# mode this CI system exists to remove. A CI preset is meant to be reproducible
# on any machine, so it reports the leak instead of absorbing it.
#
# Developer presets are untouched: exporting CXXFLAGS to try something out is a
# legitimate thing to do.

include_guard(GLOBAL)

function(cppl_ci_check_host_environment)
    if(NOT CPPL_CI)
        return()
    endif()

    set(leaked "")

    foreach(variable CFLAGS CXXFLAGS LDFLAGS CPPFLAGS LIBRARY_PATH CPATH)
        if(DEFINED ENV{${variable}} AND NOT "$ENV{${variable}}" STREQUAL "")
            list(APPEND leaked "  ${variable}=$ENV{${variable}}")
        endif()
    endforeach()

    if(NOT leaked)
        return()
    endif()

    string(REPLACE ";" "\n" report "${leaked}")

    message(FATAL_ERROR
        "\n"
        "Host environment leaks into this CI build:\n"
        "\n"
        "${report}\n"
        "\n"
        "CMake folds these into the compile and link lines, so this build would\n"
        "not match the one GitHub runs, and the difference would not be visible\n"
        "in any committed file.\n"
        "\n"
        "Run the CI preset in a clean environment:\n"
        "\n"
        "  env -u CFLAGS -u CXXFLAGS -u LDFLAGS cmake --preset <preset>\n"
        "\n"
        "tools/ci/native.sh and tools/ci/linux.sh already do this. Developer\n"
        "presets (dev, release, asan, ...) are unaffected.\n")
endfunction()
