add_library(cppl_lsp STATIC
    src/decomposition_view.cpp
    src/document.cpp
    src/editor_view.cpp
    src/hover.cpp
    src/json.cpp
    src/linter.cpp
    src/position.cpp
    src/projected_file.cpp
    src/proof_names.cpp
    src/semantic_tokens.cpp
    src/server.cpp
    src/transport.cpp
    src/uri.cpp
    include/cppl/lsp/decomposition_view.hpp
    include/cppl/lsp/diagnostic_codes.hpp
    include/cppl/lsp/document.hpp
    include/cppl/lsp/editor_view.hpp
    include/cppl/lsp/hover.hpp
    include/cppl/lsp/json.hpp
    include/cppl/lsp/linter.hpp
    include/cppl/lsp/position.hpp
    include/cppl/lsp/projected_file.hpp
    include/cppl/lsp/proof_names.hpp
    include/cppl/lsp/protocol.hpp
    include/cppl/lsp/semantic_tokens.hpp
    include/cppl/lsp/server.hpp
    include/cppl/lsp/transport.hpp
    include/cppl/lsp/uri.hpp
)

target_include_directories(cppl_lsp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cppl_lsp PUBLIC
    cppl_clang
    cppl_frontend
    cppl_diagnostics
    cppl_source
    cppl_driver
    cppl_formatter
)

target_link_libraries(cppl_lsp PRIVATE cppl_project_options)

# The Clang driver editor units are parsed with when none is configured, the
# same default the compile pipeline uses.
target_compile_definitions(cppl_lsp PRIVATE
    CPPL_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

target_compile_features(cppl_lsp PUBLIC cxx_std_20)

# Each header is analyzed on its own (cmake/HeaderChecks.cmake).
cppl_check_headers(cppl_lsp)
