# The compiler driver and the `cppl` executable.
#
# The driver orchestrates stages and owns the exit status. The library holds
# the implementation so the command-line tool does not become the compiler
# (ARCHITECTURE.md 5).
add_library(cppl_driver STATIC
    src/buffer_compile.cpp
    src/crash.cpp
    src/driver.cpp
    src/interface_io.cpp
    src/options.cpp
    src/pipeline.cpp
    src/process.cpp
    src/scratch.cpp)

target_include_directories(cppl_driver PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_driver
    PUBLIC cppl_analysis cppl_frontend cppl_clang cppl_elaboration cppl_obligations cppl_automation
           cppl_erasure cppl_diagnostics cppl_kernel cppl_artifact
    PRIVATE cppl_project_options)

target_compile_definitions(cppl_driver PRIVATE
    CPPL_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}"
    CPPL_VERSION="${PROJECT_VERSION}")

# The digest of the verifier's semantics-bearing sources, which a verification
# interface is bound to (cmake/VerifierSemantics.cmake). Recomputed whenever one
# of them changes; the header is rewritten only when the digest does.
include(VerifierSemantics)
cppl_verifier_semantics_sources("${PROJECT_SOURCE_DIR}" cppl_semantics_sources)
list(TRANSFORM cppl_semantics_sources PREPEND "${PROJECT_SOURCE_DIR}/")
set(cppl_semantics_generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(cppl_semantics_header "${cppl_semantics_generated}/cppl/driver/verifier_semantics.hpp")
set(cppl_semantics_stamp "${CMAKE_CURRENT_BINARY_DIR}/verifier_semantics.stamp")
add_custom_command(
    OUTPUT "${cppl_semantics_stamp}"
    BYPRODUCTS "${cppl_semantics_header}"
    COMMAND "${CMAKE_COMMAND}"
            "-DCPPL_SOURCE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DCPPL_OUTPUT=${cppl_semantics_header}"
            "-DCPPL_STAMP=${cppl_semantics_stamp}"
            -P "${PROJECT_SOURCE_DIR}/cmake/VerifierSemantics.cmake"
    DEPENDS ${cppl_semantics_sources} "${PROJECT_SOURCE_DIR}/cmake/VerifierSemantics.cmake"
    COMMENT "Digesting the verifier's semantics-bearing sources"
    VERBATIM)
add_custom_target(cppl_verifier_semantics DEPENDS "${cppl_semantics_stamp}")
add_dependencies(cppl_driver cppl_verifier_semantics)
target_include_directories(cppl_driver PRIVATE "${cppl_semantics_generated}")

add_library(cppl::driver ALIAS cppl_driver)

add_executable(cppl src/main.cpp)
target_link_libraries(cppl PRIVATE cppl_driver cppl_project_options)

# Each header is analyzed on its own (cmake/HeaderChecks.cmake).
cppl_check_headers(cppl_driver)
