add_library(cppl_erasure STATIC
    src/erase.cpp)

target_include_directories(cppl_erasure PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_erasure
    PUBLIC cppl_frontend cppl_diagnostics
    PRIVATE cppl_project_options)

add_library(cppl::erasure ALIAS cppl_erasure)
