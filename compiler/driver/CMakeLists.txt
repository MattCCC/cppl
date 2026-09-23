# The compiler driver and the `cppl` executable.
#
# The driver orchestrates stages and owns the exit status. The library holds
# the implementation so the command-line tool does not become the compiler
# (ARCHITECTURE.md 5).
add_library(cppl_driver STATIC
    src/buffer_compile.cpp
    src/crash.cpp
    src/driver.cpp
    src/options.cpp
    src/pipeline.cpp
    src/process.cpp
    src/scratch.cpp)

target_include_directories(cppl_driver PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_driver
    PUBLIC cppl_analysis cppl_frontend cppl_clang cppl_elaboration cppl_obligations cppl_automation
           cppl_erasure cppl_diagnostics cppl_kernel
    PRIVATE cppl_project_options)

target_compile_definitions(cppl_driver PRIVATE
    CPPL_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}"
    CPPL_VERSION="${PROJECT_VERSION}")

add_library(cppl::driver ALIAS cppl_driver)

add_executable(cppl src/main.cpp)
target_link_libraries(cppl PRIVATE cppl_driver cppl_project_options)
