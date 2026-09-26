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
    src/scratch.cpp
    src/version.cpp)

target_include_directories(cppl_driver PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_driver
    PUBLIC cppl_analysis cppl_frontend cppl_clang cppl_elaboration cppl_obligations cppl_automation
           cppl_erasure cppl_diagnostics cppl_kernel cppl_artifact
    PRIVATE cppl_project_options)

target_compile_definitions(cppl_driver PRIVATE
    CPPL_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}"
    CPPL_VERSION="${PROJECT_VERSION}")

# The verifier semantics digest of the sources this compiler is built from,
# which every verification interface it writes records and every one it reads
# must match (cmake/VerifierSemanticsSources.cmake, SPEC.md TUBOUND-005, TRUST.md
# TCB-XTU-008). It is computed when the project is configured, so the header
# exists for analysis before anything is built, and again whenever a semantic
# source changes.
include("${PROJECT_SOURCE_DIR}/cmake/VerifierSemanticsSources.cmake")
cppl_component_sources("${PROJECT_SOURCE_DIR}" "${CPPL_SEMANTIC_COMPONENTS}" cppl_semantic_sources)
list(TRANSFORM cppl_semantic_sources PREPEND "${PROJECT_SOURCE_DIR}/")
set(cppl_semantics_script "${PROJECT_SOURCE_DIR}/cmake/ComputeVerifierSemantics.cmake")
set(cppl_semantics_header "${CMAKE_BINARY_DIR}/generated/cppl/verifier_semantics.hpp")
set(cppl_semantics_stamp "${CMAKE_BINARY_DIR}/generated/verifier_semantics.stamp")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}" "-DHEADER=${cppl_semantics_header}"
            -P "${cppl_semantics_script}"
    RESULT_VARIABLE cppl_semantics_result)
if(NOT cppl_semantics_result EQUAL 0)
    message(FATAL_ERROR "the verifier semantics digest could not be computed")
endif()
add_custom_command(
    OUTPUT "${cppl_semantics_stamp}"
    BYPRODUCTS "${cppl_semantics_header}"
    COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}" "-DHEADER=${cppl_semantics_header}"
            "-DSTAMP=${cppl_semantics_stamp}" -P "${cppl_semantics_script}"
    DEPENDS ${cppl_semantic_sources} "${cppl_semantics_script}"
            "${PROJECT_SOURCE_DIR}/cmake/VerifierSemanticsSources.cmake"
    COMMENT "Computing the verifier semantics digest"
    VERBATIM)
add_custom_target(cppl_verifier_semantics DEPENDS "${cppl_semantics_stamp}")
add_dependencies(cppl_driver cppl_verifier_semantics)
target_include_directories(cppl_driver PRIVATE "${CMAKE_BINARY_DIR}/generated")

# The commit the tree is at and whether a tracked file differs from it, which
# `cppl --cppl-version` reports (cmake/SourceIdentity.cmake). Asked on every
# build, since a commit changes nothing CMake watches; the header is rewritten
# only when the answer changes, and only src/version.cpp includes it.
find_program(CPPL_GIT_EXECUTABLE NAMES git)
mark_as_advanced(CPPL_GIT_EXECUTABLE)
set(cppl_identity_header "${CMAKE_BINARY_DIR}/generated/cppl/driver/source_identity.hpp")
set(cppl_git "")
if(CPPL_GIT_EXECUTABLE)
    set(cppl_git "${CPPL_GIT_EXECUTABLE}")
endif()
add_custom_target(cppl_source_identity
    COMMAND "${CMAKE_COMMAND}"
            "-DCPPL_SOURCE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DCPPL_GIT=${cppl_git}"
            "-DCPPL_OUTPUT=${cppl_identity_header}"
            -P "${PROJECT_SOURCE_DIR}/cmake/SourceIdentity.cmake"
    BYPRODUCTS "${cppl_identity_header}"
    COMMENT "Recording the source identity of this build"
    VERBATIM)
add_dependencies(cppl_driver cppl_source_identity)

add_library(cppl::driver ALIAS cppl_driver)

add_executable(cppl src/main.cpp)
target_link_libraries(cppl PRIVATE cppl_driver cppl_project_options)

# Each header is analyzed on its own (cmake/HeaderChecks.cmake).
cppl_check_headers(cppl_driver)
