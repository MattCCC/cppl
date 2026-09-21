add_library(cppl_analysis STATIC src/analyze.cpp)
target_include_directories(cppl_analysis PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(cppl_analysis PUBLIC cppl_frontend cppl_clang cppl_elaboration cppl_decomposition PRIVATE cppl_project_options)
