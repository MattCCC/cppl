# Test suites, divided by responsibility (ARCHITECTURE.md 61).
#
#   unit          isolated implementation behaviour
#   kernel        the proof rules themselves, without the rest of the compiler
#   negative      programs and proofs that must be rejected
#   conformance   valid supported C++ stays valid C++L, in every target standard
#   integration   the driver, arguments, headers, reproducibility
#   e2e           source in, verified native executable out
#   architecture  dependency direction

add_library(cppl_testing STATIC support/src/test.cpp)
target_include_directories(cppl_testing PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/support/include>)
target_link_libraries(cppl_testing PRIVATE cppl_project_options)

function(cppl_add_unit_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE cppl_testing cppl_project_options)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

cppl_add_unit_test(kernel_check_test kernel/check_test.cpp)
target_link_libraries(kernel_check_test PRIVATE cppl_kernel)

cppl_add_unit_test(kernel_paths_test kernel/paths_test.cpp)
target_link_libraries(kernel_paths_test PRIVATE cppl_kernel)

cppl_add_unit_test(kernel_arithmetic_test kernel/arithmetic_test.cpp)
target_link_libraries(kernel_arithmetic_test PRIVATE cppl_kernel)

cppl_add_unit_test(unit_digest_test unit/digest_test.cpp)
target_link_libraries(unit_digest_test PRIVATE cppl_source)

cppl_add_unit_test(unit_lexer_test unit/lexer_test.cpp)
target_link_libraries(unit_lexer_test PRIVATE cppl_frontend)

cppl_add_unit_test(unit_recognizer_test unit/recognizer_test.cpp)
target_link_libraries(unit_recognizer_test PRIVATE cppl_frontend cppl_diagnostics)

cppl_add_unit_test(unit_projection_test unit/projection_test.cpp)
target_link_libraries(unit_projection_test PRIVATE cppl_frontend cppl_erasure cppl_diagnostics)

cppl_add_unit_test(unit_vir_test unit/vir_test.cpp)
target_link_libraries(unit_vir_test PRIVATE cppl_vir)

cppl_add_unit_test(unit_verdict_test unit/verdict_test.cpp)
target_link_libraries(unit_verdict_test PRIVATE cppl_kernel cppl_obligations)

cppl_add_unit_test(unit_contracts_test unit/contracts_test.cpp)
target_link_libraries(unit_contracts_test PRIVATE cppl_automation)

cppl_add_unit_test(unit_arithmetic_test unit/arithmetic_test.cpp)
target_link_libraries(unit_arithmetic_test PRIVATE cppl_automation)

# Driver-level suites. They exercise the installed command exactly as a
# developer or CI would.
set(CPPL_TEST_FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set(CPPL_TEST_WORK "${CMAKE_CURRENT_BINARY_DIR}/work")

function(cppl_add_script_test name script)
    add_test(NAME ${name}
             COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/${script}"
                     $<TARGET_FILE:cppl> "${CPPL_TEST_FIXTURES}" "${CPPL_TEST_WORK}"
                     "${CPPL_DEFAULT_CLANG}")
endfunction()

cppl_add_script_test(conformance_ordinary_cpp conformance/ordinary_cpp.sh)
cppl_add_script_test(conformance_contextual_identifiers conformance/contextual_identifiers.sh)
cppl_add_script_test(conformance_target_standard_isolation conformance/target_standard_isolation.sh)

# The documented examples are compiled by the compiler they document.
add_test(NAME conformance_readme_examples
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/conformance/readme_examples.sh"
                 $<TARGET_FILE:cppl> "${CMAKE_SOURCE_DIR}/README.md" "${CPPL_TEST_WORK}")

cppl_add_script_test(e2e_valid_law e2e/valid_law.sh)
cppl_add_script_test(e2e_invalid_law e2e/invalid_law.sh)
cppl_add_script_test(e2e_written_proof e2e/written_proof.sh)
cppl_add_script_test(e2e_instantiated_proof e2e/instantiated_proof.sh)
cppl_add_script_test(e2e_conditional_proof e2e/conditional_proof.sh)
cppl_add_script_test(e2e_rewritten_proof e2e/rewritten_proof.sh)
cppl_add_script_test(e2e_verified_functions e2e/verified_functions.sh)
cppl_add_script_test(e2e_verified_calls e2e/verified_calls.sh)
cppl_add_script_test(e2e_verified_paths e2e/verified_paths.sh)
cppl_add_script_test(e2e_verified_locals e2e/verified_locals.sh)
cppl_add_script_test(e2e_verified_arithmetic e2e/verified_arithmetic.sh)

cppl_add_script_test(negative_broken_implementation negative/broken_implementation.sh)
cppl_add_script_test(negative_unproven_dependencies negative/unproven_dependencies.sh)
cppl_add_script_test(negative_written_proofs negative/written_proofs.sh)
cppl_add_script_test(negative_verified_functions negative/verified_functions.sh)
cppl_add_script_test(negative_verified_calls negative/verified_calls.sh)
cppl_add_script_test(negative_verified_paths negative/verified_paths.sh)
cppl_add_script_test(negative_verified_locals negative/verified_locals.sh)

cppl_add_script_test(integration_header_law integration/header_law.sh)
cppl_add_script_test(integration_determinism integration/determinism.sh)
cppl_add_script_test(integration_parallel_verification integration/parallel_verification.sh)

add_test(NAME architecture_dependency_boundaries
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/architecture/dependency_boundaries.sh"
                 "${CMAKE_SOURCE_DIR}/kernel" "$<TARGET_FILE:cppl_kernel>")
