cmake_minimum_required(VERSION 3.25)

project(
    cppl
    VERSION 0.0.1
    DESCRIPTION "C++L - C++ with Laws"
    LANGUAGES C CXX
)

# -----------------------------------------------------------------------------
# CMake modules
# -----------------------------------------------------------------------------

list(
    APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
)

# -----------------------------------------------------------------------------
# C++ configuration
# -----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(
    CMAKE_RUNTIME_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/bin"
)

set(
    CMAKE_LIBRARY_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/lib"
)

set(
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/lib"
)

if(
    NOT CMAKE_BUILD_TYPE
    AND NOT CMAKE_CONFIGURATION_TYPES
)
    set(
        CMAKE_BUILD_TYPE
        RelWithDebInfo
        CACHE STRING
        "Build type"
        FORCE
    )
endif()

# -----------------------------------------------------------------------------
# Project options
# -----------------------------------------------------------------------------

option(
    CPPL_BUILD_TESTS
    "Build the C++L test suites"
    ON
)

option(
    CPPL_WARNINGS_AS_ERRORS
    "Treat compiler warnings as errors"
    OFF
)

option(
    CPPL_ENABLE_ASAN
    "Enable AddressSanitizer"
    OFF
)

option(
    CPPL_ENABLE_UBSAN
    "Enable UndefinedBehaviorSanitizer"
    OFF
)

option(
    CPPL_ENABLE_TSAN
    "Enable ThreadSanitizer"
    OFF
)

# -----------------------------------------------------------------------------
# Common project compile options
# -----------------------------------------------------------------------------

add_library(cppl_project_options INTERFACE)

if(MSVC)
    target_compile_options(
        cppl_project_options
        INTERFACE
            /W4
            /permissive-
    )

    if(CPPL_WARNINGS_AS_ERRORS)
        target_compile_options(
            cppl_project_options
            INTERFACE
                /WX
        )
    endif()
else()
    target_compile_options(
        cppl_project_options
        INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wconversion
            -Wsign-conversion
    )

    if(CPPL_WARNINGS_AS_ERRORS)
        target_compile_options(
            cppl_project_options
            INTERFACE
                -Werror
        )
    endif()
endif()

# -----------------------------------------------------------------------------
# Sanitizers
# -----------------------------------------------------------------------------

if(
    CPPL_ENABLE_TSAN
    AND (CPPL_ENABLE_ASAN OR CPPL_ENABLE_UBSAN)
)
    message(
        FATAL_ERROR
        "ThreadSanitizer cannot be combined with ASan or UBSan."
    )
endif()

if(CPPL_ENABLE_ASAN)
    if(MSVC)
        message(
            FATAL_ERROR
            "CPPL_ENABLE_ASAN is not configured for MSVC."
        )
    endif()

    target_compile_options(
        cppl_project_options
        INTERFACE
            -fsanitize=address
            -fno-omit-frame-pointer
    )

    target_link_options(
        cppl_project_options
        INTERFACE
            -fsanitize=address
    )
endif()

if(CPPL_ENABLE_UBSAN)
    if(MSVC)
        message(
            FATAL_ERROR
            "CPPL_ENABLE_UBSAN is not configured for MSVC."
        )
    endif()

    target_compile_options(
        cppl_project_options
        INTERFACE
            -fsanitize=undefined
            -fno-omit-frame-pointer
    )

    target_link_options(
        cppl_project_options
        INTERFACE
            -fsanitize=undefined
    )
endif()

if(CPPL_ENABLE_TSAN)
    if(MSVC)
        message(
            FATAL_ERROR
            "CPPL_ENABLE_TSAN is not configured for MSVC."
        )
    endif()

    target_compile_options(
        cppl_project_options
        INTERFACE
            -fsanitize=thread
            -fno-omit-frame-pointer
    )

    target_link_options(
        cppl_project_options
        INTERFACE
            -fsanitize=thread
    )
endif()

# -----------------------------------------------------------------------------
# Clang / LLVM
# -----------------------------------------------------------------------------
#
# LibClang is the semantic bridge used by C++L.
#
# The Clang driver resolved by FindLibClang.cmake is also made the authoritative
# LLVM installation for auxiliary tooling such as clang-format and clang-tidy.
#
# This preserves the single-toolchain invariant:
#
#   libclang
#   clang++
#   clang-format
#   clang-tidy
#
# should all belong to the same LLVM major version.

find_package(LibClang REQUIRED)

if(
    NOT DEFINED LibClang_CLANG_EXECUTABLE
    OR LibClang_CLANG_EXECUTABLE STREQUAL ""
)
    message(
        FATAL_ERROR
        "LibClang was found, but FindLibClang.cmake did not provide "
        "LibClang_CLANG_EXECUTABLE."
    )
endif()

if(NOT EXISTS "${LibClang_CLANG_EXECUTABLE}")
    message(
        FATAL_ERROR
        "The Clang driver resolved by FindLibClang.cmake does not exist:\n"
        "  ${LibClang_CLANG_EXECUTABLE}"
    )
endif()

set(
    CPPL_DEFAULT_CLANG
    "${LibClang_CLANG_EXECUTABLE}"
    CACHE FILEPATH
    "Clang driver used for preprocessing and native code generation"
    FORCE
)

set(
    CPPL_CLANG_DRIVER
    "${CPPL_DEFAULT_CLANG}"
    CACHE INTERNAL
    "Authoritative Clang driver selected by C++L"
    FORCE
)

message(
    STATUS
    "C++L: libclang     ${LibClang_LIBRARY}"
)

message(
    STATUS
    "C++L: clang driver ${CPPL_CLANG_DRIVER}"
)

# IMPORTANT:
# LLVMToolchain must be included only after CPPL_CLANG_DRIVER has been set.
#
# Formatting and linting depend on LLVMToolchain.

include(LLVMToolchain)
include(Formatting)
include(Linting)

# -----------------------------------------------------------------------------
# C++L components
# -----------------------------------------------------------------------------

# Source identity is a leaf used by both the VIR and Clang bridge.

add_subdirectory(compiler/source)
add_subdirectory(kernel)
add_subdirectory(vir)
add_subdirectory(clang)
add_subdirectory(compiler)
add_subdirectory(tools/cppl-lsp)
add_subdirectory(src/lsp)

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------

if(CPPL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# -----------------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------------
#
# Only the public C++L executable is installed.
#
# Internal libraries, tests, VIR components and build-system artifacts remain
# inside the build tree.

install(
    TARGETS cppl
    RUNTIME DESTINATION bin
)

# -----------------------------------------------------------------------------
# Packaging
# -----------------------------------------------------------------------------

set(
    CPACK_PACKAGE_NAME
    "cppl"
)

set(
    CPACK_PACKAGE_VENDOR
    "C++L"
)

set(
    CPACK_PACKAGE_VERSION
    "${PROJECT_VERSION}"
)

set(
    CPACK_PACKAGE_DIRECTORY
    "${CMAKE_BINARY_DIR}/packages"
)

if(WIN32)
    set(
        CPACK_GENERATOR
        "ZIP"
    )
else()
    set(
        CPACK_GENERATOR
        "TGZ"
    )
endif()

include(CPack)