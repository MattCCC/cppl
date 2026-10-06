# The canonical C++L formatter.
#
# One engine, shared by `cppl-format` (tools/cppl-format) and `cppl-lsp`
# (src/lsp): both call `cppl::formatter::format()` and get byte-identical
# output. Ordinary C++ formatting is delegated to `clang-format`; this library
# only relocates C++L-specific clauses (SPEC.md 10, 11; GRAMMAR.md 4, 8, 25,
# 26).
add_library(cppl_formatter STATIC
    src/format.cpp
    src/format_arms.cpp
    src/format_clang.cpp
    src/format_clauses.cpp)

target_include_directories(cppl_formatter PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_formatter
    PUBLIC cppl_frontend cppl_diagnostics cppl_source
    PRIVATE cppl_driver cppl_project_options)

# The clang-format the formatter runs: the one beside the Clang driver the
# compiler runs, where that installation has one, so that an installed
# formatter reaches its LLVM through the same path the compiler does; otherwise
# the one the build formats its own sources with (cmake/Formatting.cmake).
get_filename_component(cppl_driver_directory "${CPPL_DEFAULT_CLANG}" DIRECTORY)
set(cppl_runtime_clang_format "${CPPL_CLANG_FORMAT}")
if(EXISTS "${cppl_driver_directory}/clang-format${CMAKE_EXECUTABLE_SUFFIX}")
    set(cppl_runtime_clang_format "${cppl_driver_directory}/clang-format${CMAKE_EXECUTABLE_SUFFIX}")
endif()
target_compile_definitions(cppl_formatter PRIVATE
    CPPL_DEFAULT_CLANG_FORMAT="${cppl_runtime_clang_format}")

# The canonical style is the repository's `.clang-format`, built into the
# formatter rather than read from the source tree, so that an installed
# `cppl-format` or `cppl-lsp` needs no file of the tree it was built from and
# formats exactly as the build tree does. Reconfigured when the file changes;
# the header is rewritten only when its content would.
file(READ "${CPPL_CLANG_FORMAT_CONFIG}" cppl_canonical_style)
if(cppl_canonical_style MATCHES "\\)cppl_style\"")
    message(FATAL_ERROR "${CPPL_CLANG_FORMAT_CONFIG} holds the delimiter its embedding uses")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CPPL_CLANG_FORMAT_CONFIG}")
set(cppl_formatter_generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(CONFIGURE
    OUTPUT "${cppl_formatter_generated}/cppl/formatter/canonical_style.hpp"
    CONTENT "#pragma once

#include <string_view>

namespace cppl::formatter {

// Generated from the repository's .clang-format by compiler/formatter/CMakeLists.txt. Do not edit.
inline constexpr std::string_view kCanonicalStyle = R\"cppl_style(@cppl_canonical_style@)cppl_style\";

} // namespace cppl::formatter
"
    @ONLY)
target_include_directories(cppl_formatter PRIVATE "${cppl_formatter_generated}")

add_library(cppl::formatter ALIAS cppl_formatter)

# Each header is analyzed on its own (cmake/HeaderChecks.cmake).
cppl_check_headers(cppl_formatter)
