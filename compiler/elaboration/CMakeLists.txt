add_library(cppl_elaboration STATIC
    src/elaborate.cpp)

target_include_directories(cppl_elaboration PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_elaboration
    PUBLIC cppl_vir cppl_decomposition cppl_clang cppl_frontend cppl_diagnostics
    PRIVATE cppl_project_options)

add_library(cppl::elaboration ALIAS cppl_elaboration)

# Each header is analyzed on its own (cmake/HeaderChecks.cmake).
cppl_check_headers(cppl_elaboration)
