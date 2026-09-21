add_library(cppl_lsp STATIC
    src/document.cpp
    src/json.cpp
    src/linter.cpp
    src/position.cpp
    src/server.cpp
    src/transport.cpp
    src/uri.cpp
    include/cppl/lsp/diagnostic_codes.hpp
    include/cppl/lsp/document.hpp
    include/cppl/lsp/json.hpp
    include/cppl/lsp/linter.hpp
    include/cppl/lsp/position.hpp
    include/cppl/lsp/protocol.hpp
    include/cppl/lsp/server.hpp
    include/cppl/lsp/transport.hpp
    include/cppl/lsp/uri.hpp
)

target_include_directories(cppl_lsp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cppl_lsp PUBLIC
    cppl_frontend
    cppl_diagnostics
    cppl_source
    cppl_driver
    cppl_formatter
)

target_compile_features(cppl_lsp PUBLIC cxx_std_20)
