# cmake/ci/Environment.cmake
#
# Environment reporting for CI parity.
#
# When a build behaves differently on GitHub than it does locally, the cause is
# almost always something in this banner: a different compiler, a different
# LLVM, a different generator, or a different CMake.
#
# The banner is printed once per configure. Ordinary developer presets stay
# quiet; it is enabled by CPPL_CI, which every ci-* preset sets.

include_guard(GLOBAL)

function(cppl_ci_report_environment)
    if(NOT CPPL_CI)
        return()
    endif()

    message(STATUS "")
    message(STATUS "C++L CI environment")
    message(STATUS "-------------------")

    message(STATUS "  preset          $ENV{CPPL_CI_PRESET}")
    message(STATUS "  host system     ${CMAKE_HOST_SYSTEM_NAME} ${CMAKE_HOST_SYSTEM_VERSION}")
    message(STATUS "  host arch       ${CMAKE_HOST_SYSTEM_PROCESSOR}")
    message(STATUS "  target system   ${CMAKE_SYSTEM_NAME}")
    message(STATUS "  cmake           ${CMAKE_VERSION} (${CMAKE_COMMAND})")
    message(STATUS "  generator       ${CMAKE_GENERATOR}")
    message(STATUS "  build type      ${CMAKE_BUILD_TYPE}")
    message(STATUS "  binary dir      ${CMAKE_BINARY_DIR}")

    message(STATUS "  C compiler      ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
    message(STATUS "                  ${CMAKE_C_COMPILER}")
    message(STATUS "  C++ compiler    ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    message(STATUS "                  ${CMAKE_CXX_COMPILER}")
    message(STATUS "  C++ standard    ${CMAKE_CXX_STANDARD}")

    message(STATUS "  libclang        ${LibClang_VERSION}")
    message(STATUS "                  ${LibClang_LIBRARY}")
    message(STATUS "  LLVM root       ${LibClang_ROOT_DIR}")
    message(STATUS "  clang driver    ${CPPL_CLANG_DRIVER}")

    if(CPPL_CLANG_FORMAT)
        cppl_ci_tool_version("${CPPL_CLANG_FORMAT}" _format_version)
        message(STATUS "  clang-format    ${_format_version}")
        message(STATUS "                  ${CPPL_CLANG_FORMAT}")
    endif()

    if(CPPL_CLANG_TIDY)
        cppl_ci_tool_version("${CPPL_CLANG_TIDY}" _tidy_version)
        message(STATUS "  clang-tidy      ${_tidy_version}")
        message(STATUS "                  ${CPPL_CLANG_TIDY}")
    endif()

    message(STATUS "  warnings=errors ${CPPL_WARNINGS_AS_ERRORS}")
    message(STATUS "  sanitizers      asan=${CPPL_ENABLE_ASAN} ubsan=${CPPL_ENABLE_UBSAN} tsan=${CPPL_ENABLE_TSAN}")
    message(STATUS "")
endfunction()

function(cppl_ci_tool_version TOOL OUT_VAR)
    execute_process(
        COMMAND "${TOOL}" --version
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _out
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT _rc EQUAL 0)
        set(${OUT_VAR} "unknown" PARENT_SCOPE)
        return()
    endif()

    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" _version "${_out}")

    if(_version)
        set(${OUT_VAR} "${_version}" PARENT_SCOPE)
    else()
        set(${OUT_VAR} "unknown" PARENT_SCOPE)
    endif()
endfunction()
