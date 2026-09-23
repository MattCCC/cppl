add_library(cppl_diagnostics STATIC
    src/diagnostic.cpp)

target_include_directories(cppl_diagnostics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_diagnostics
    PUBLIC cppl_source
    PRIVATE cppl_project_options)

add_library(cppl::diagnostics ALIAS cppl_diagnostics)

# Each header is analyzed on its own (cmake/HeaderChecks.cmake).
cppl_check_headers(cppl_diagnostics)
