cmake_minimum_required(VERSION 3.24)

project(cppl
    VERSION 0.1.0
    DESCRIPTION "C++L - C++ with Laws"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CPPL_BUILD_TESTS "Build the C++L test suites" ON)
option(CPPL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(cppl_project_options INTERFACE)
target_compile_options(cppl_project_options INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
if(CPPL_WARNINGS_AS_ERRORS)
    target_compile_options(cppl_project_options INTERFACE -Werror)
endif()

# The Clang/LLVM installation supplies both the semantic bridge (libclang) and
# the native compilation driver. Keeping them from one installation means the
# semantics C++L verifies and the semantics Clang compiles come from one
# toolchain (ARCHITECTURE.md "Single-projection invariant").
find_package(LibClang REQUIRED)

set(CPPL_DEFAULT_CLANG "${LibClang_CLANG_EXECUTABLE}" CACHE FILEPATH
    "clang++ driver used for preprocessing and native code generation")

message(STATUS "C++L: libclang     ${LibClang_LIBRARY}")
message(STATUS "C++L: clang driver ${CPPL_DEFAULT_CLANG}")

# Source identity is a leaf: the VIR and the Clang bridge both use it, so it is
# configured before them.
add_subdirectory(compiler/source)
add_subdirectory(kernel)
add_subdirectory(vir)
add_subdirectory(clang)
add_subdirectory(compiler)

install(
    TARGETS cppl
    RUNTIME DESTINATION bin
)

set(CPACK_PACKAGE_NAME "cppl")
set(CPACK_PACKAGE_VENDOR "C++L")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")

set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)

if(CPPL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
