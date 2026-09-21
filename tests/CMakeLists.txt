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

cppl_add_unit_test(kernel_value_model_test kernel/value_model_test.cpp)
target_link_libraries(kernel_value_model_test PRIVATE cppl_kernel)

cppl_add_unit_test(kernel_check_test kernel/check_test.cpp)
target_link_libraries(kernel_check_test PRIVATE cppl_kernel)

cppl_add_unit_test(kernel_malformed_test kernel/malformed_test.cpp)
target_link_libraries(kernel_malformed_test PRIVATE cppl_kernel)

cppl_add_unit_test(kernel_substitution_property_test kernel/substitution_property_test.cpp)
target_link_libraries(kernel_substitution_property_test PRIVATE cppl_kernel)

cppl_add_unit_test(kernel_evidence_regression_test kernel/evidence_regression_test.cpp)
target_link_libraries(kernel_evidence_regression_test PRIVATE cppl_kernel)

cppl_add_unit_test(kernel_proof_fuzz_test kernel/proof_fuzz_test.cpp)
target_link_libraries(kernel_proof_fuzz_test PRIVATE cppl_kernel)

cppl_add_unit_test(kernel_paths_test kernel/paths_test.cpp)
target_link_libraries(kernel_paths_test PRIVATE cppl_kernel)

cppl_add_unit_test(kernel_arithmetic_test kernel/arithmetic_test.cpp)
target_link_libraries(kernel_arithmetic_test PRIVATE cppl_kernel)

cppl_add_unit_test(kernel_adversarial_test kernel/adversarial_kernel_test.cpp)
target_link_libraries(kernel_adversarial_test PRIVATE cppl_kernel)

cppl_add_unit_test(unit_digest_test unit/digest_test.cpp)
target_link_libraries(unit_digest_test PRIVATE cppl_source)

cppl_add_unit_test(unit_lexer_test unit/lexer_test.cpp)
target_link_libraries(unit_lexer_test PRIVATE cppl_frontend)

cppl_add_unit_test(unit_recognizer_test unit/recognizer_test.cpp)
target_link_libraries(unit_recognizer_test PRIVATE cppl_frontend cppl_diagnostics)

cppl_add_unit_test(unit_projection_test unit/projection_test.cpp)
target_link_libraries(unit_projection_test PRIVATE cppl_frontend cppl_erasure cppl_diagnostics)

cppl_add_unit_test(unit_frontend_property_test unit/frontend_property_test.cpp)
target_link_libraries(unit_frontend_property_test PRIVATE cppl_frontend cppl_erasure cppl_diagnostics)

cppl_add_unit_test(unit_vir_test unit/vir_test.cpp)
target_link_libraries(unit_vir_test PRIVATE cppl_vir)

cppl_add_unit_test(unit_verdict_test unit/verdict_test.cpp)
target_link_libraries(unit_verdict_test PRIVATE cppl_kernel cppl_obligations)

cppl_add_unit_test(unit_contracts_test unit/contracts_test.cpp)
target_link_libraries(unit_contracts_test PRIVATE cppl_automation)

cppl_add_unit_test(unit_arithmetic_test unit/arithmetic_test.cpp)
target_link_libraries(unit_arithmetic_test PRIVATE cppl_automation)

cppl_add_unit_test(unit_formal_equality_test unit/formal_equality_test.cpp)
target_link_libraries(unit_formal_equality_test PRIVATE cppl_automation)

cppl_add_unit_test(unit_quantified_propositions_test unit/quantified_propositions_test.cpp)
target_link_libraries(unit_quantified_propositions_test PRIVATE cppl_automation)
# The generic case engine, then one suite per provider for its state model.
cppl_add_unit_test(unit_decomposition_test unit/decomposition_test.cpp)
target_link_libraries(unit_decomposition_test PRIVATE cppl_obligations)

cppl_add_unit_test(unit_scoped_enum_provider_test unit/scoped_enum_provider_test.cpp)
target_link_libraries(unit_scoped_enum_provider_test PRIVATE cppl_decomposition)

# cppl-lsp: the language server library, exercised directly rather than
# through a real subprocess/stdio pipe (ARCHITECTURE.md 61's "unit" tier).
cppl_add_unit_test(lsp_document_test unit/lsp_document_test.cpp)
target_link_libraries(lsp_document_test PRIVATE cppl_lsp)

cppl_add_unit_test(lsp_linter_test unit/lsp_linter_test.cpp)
target_link_libraries(lsp_linter_test PRIVATE cppl_lsp)

cppl_add_unit_test(lsp_position_test unit/lsp_position_test.cpp)
target_link_libraries(lsp_position_test PRIVATE cppl_lsp)

cppl_add_unit_test(lsp_server_test unit/lsp_server_test.cpp)
target_link_libraries(lsp_server_test PRIVATE cppl_lsp)

cppl_add_unit_test(lsp_uri_test unit/lsp_uri_test.cpp)
target_link_libraries(lsp_uri_test PRIVATE cppl_lsp)

cppl_add_unit_test(lsp_json_test unit/lsp_json_test.cpp)
target_link_libraries(lsp_json_test PRIVATE cppl_lsp)

cppl_add_unit_test(lsp_transport_test unit/lsp_transport_test.cpp)
target_link_libraries(lsp_transport_test PRIVATE cppl_lsp)

# Proves the LSP formatting handlers and the shared cppl::formatter engine
# they wrap produce byte-identical output (ARCHITECTURE.md: one engine, not
# a parallel implementation per surface).
cppl_add_unit_test(lsp_formatting_test unit/lsp_formatting_test.cpp)
target_link_libraries(lsp_formatting_test PRIVATE cppl_lsp cppl_formatter)

# Driver-level suites. They exercise the installed command exactly as a
# developer or CI would.
set(CPPL_TEST_FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set(CPPL_TEST_WORK "${CMAKE_CURRENT_BINARY_DIR}/work")

# compiler/formatter: the one canonical-formatting engine shared by cppl-lsp
# and the cppl-format CLI. Exercised directly against inline C++L snippets
# and, for the idempotency property, against the real fixture corpus above.
cppl_add_unit_test(formatter_test unit/formatter_test.cpp)
target_link_libraries(formatter_test PRIVATE cppl_formatter)
target_compile_definitions(formatter_test PRIVATE
    CPPL_TEST_FIXTURES_DIR="${CPPL_TEST_FIXTURES}")

# The primary acceptance invariant (task spec): the LSP pipeline must not
# introduce a diagnostic that the CLI itself would not, over the same real
# fixtures the driver-level suites above already exercise.
cppl_add_unit_test(lsp_fixtures_test unit/lsp_fixtures_test.cpp)
target_link_libraries(lsp_fixtures_test PRIVATE cppl_lsp)
target_compile_definitions(lsp_fixtures_test PRIVATE
    CPPL_TEST_FIXTURES_DIR="${CPPL_TEST_FIXTURES}"
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

function(cppl_add_script_test name script)
    add_test(NAME ${name}
             COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/${script}"
                     $<TARGET_FILE:cppl> "${CPPL_TEST_FIXTURES}" "${CPPL_TEST_WORK}"
                     "${CPPL_DEFAULT_CLANG}")
endfunction()

# cppl-format: the CLI surface of compiler/formatter (--check/-i), exercised
# as an installed command exactly as a developer or CI would invoke it.
add_test(NAME format_check
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/format/format_check.sh"
                 $<TARGET_FILE:cppl-format> "${CPPL_TEST_FIXTURES}" "${CPPL_TEST_WORK}")

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
cppl_add_script_test(e2e_formal_equality e2e/formal_equality.sh)
cppl_add_script_test(negative_formal_equality negative/formal_equality.sh)
cppl_add_script_test(e2e_quantified_propositions e2e/quantified_propositions.sh)
cppl_add_script_test(e2e_logical_composition e2e/logical_composition.sh)
cppl_add_script_test(e2e_disjunction e2e/disjunction.sh)
cppl_add_script_test(negative_quantified_propositions negative/quantified_propositions.sh)
cppl_add_script_test(e2e_verified_storage e2e/verified_storage.sh)
cppl_add_script_test(negative_verified_storage negative/verified_storage.sh)
cppl_add_script_test(e2e_refinement_types e2e/refinement_types.sh)
cppl_add_script_test(e2e_refinement_flow e2e/refinement_flow.sh)
cppl_add_script_test(negative_refinement_types negative/refinement_types.sh)
cppl_add_script_test(e2e_enum_cases e2e/enum_cases.sh)
cppl_add_script_test(negative_proof_cases negative/proof_cases.sh)
cppl_add_script_test(e2e_structural_cases e2e/structural_cases.sh)
cppl_add_script_test(negative_structural_cases negative/structural_cases.sh)
cppl_add_script_test(e2e_instantiated_proof e2e/instantiated_proof.sh)
cppl_add_script_test(e2e_conditional_proof e2e/conditional_proof.sh)
cppl_add_script_test(e2e_rewritten_proof e2e/rewritten_proof.sh)
cppl_add_script_test(e2e_verified_functions e2e/verified_functions.sh)
cppl_add_script_test(e2e_verified_calls e2e/verified_calls.sh)
cppl_add_script_test(e2e_verified_paths e2e/verified_paths.sh)
cppl_add_script_test(e2e_verified_locals e2e/verified_locals.sh)
cppl_add_script_test(e2e_verified_arithmetic e2e/verified_arithmetic.sh)
cppl_add_script_test(e2e_verified_loops e2e/verified_loops.sh)

cppl_add_script_test(negative_broken_implementation negative/broken_implementation.sh)
cppl_add_script_test(negative_unproven_dependencies negative/unproven_dependencies.sh)
cppl_add_script_test(negative_written_proofs negative/written_proofs.sh)
cppl_add_script_test(negative_verified_functions negative/verified_functions.sh)
cppl_add_script_test(negative_verified_calls negative/verified_calls.sh)
cppl_add_script_test(negative_verified_paths negative/verified_paths.sh)
cppl_add_script_test(negative_verified_locals negative/verified_locals.sh)
cppl_add_script_test(negative_verified_loops negative/verified_loops.sh)

cppl_add_script_test(integration_header_law integration/header_law.sh)
cppl_add_script_test(integration_source_identity integration/source_identity.sh)
cppl_add_script_test(integration_determinism integration/determinism.sh)
cppl_add_script_test(integration_parallel_verification integration/parallel_verification.sh)

add_test(NAME architecture_dependency_boundaries
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/architecture/dependency_boundaries.sh"
                 "${CMAKE_SOURCE_DIR}/kernel" "$<TARGET_FILE:cppl_kernel>")
