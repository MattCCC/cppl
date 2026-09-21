# The canonical C++L formatter.
#
# One engine, shared by `cppl-format` (tools/cppl-format) and `cppl-lsp`
# (src/lsp): both call `cppl::formatter::format()` and get byte-identical
# output. Ordinary C++ formatting is delegated to `clang-format`; this library
# only relocates C++L-specific clauses (SPEC.md 10, 11; GRAMMAR.md 4, 8, 25,
# 26).
add_library(cppl_formatter STATIC
    src/format.cpp)

target_include_directories(cppl_formatter PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_formatter
    PUBLIC cppl_frontend cppl_diagnostics cppl_source
    PRIVATE cppl_driver cppl_project_options)

target_compile_definitions(cppl_formatter PRIVATE
    CPPL_DEFAULT_CLANG_FORMAT="${CPPL_CLANG_FORMAT}"
    CPPL_REPO_CLANG_FORMAT_CONFIG="${CPPL_CLANG_FORMAT_CONFIG}")

add_library(cppl::formatter ALIAS cppl_formatter)
