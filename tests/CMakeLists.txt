# Test suites, divided by responsibility (ARCHITECTURE.md 82).
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
# Only the harness's own headers: the fixtures are programs under test.
cppl_check_headers(cppl_testing support)

# Labels are what CTest selects a tier with (`ctest -L unit`). They are applied
# at registration so no list of tests is maintained anywhere else -- not in the
# Makefile, and not in a CI workflow.
#
# The tier is derived from the test name's prefix, which already encodes the
# responsibility split documented above. `kernel_*` is a compiled in-process
# suite, so it carries both its own tier and `unit`.
function(cppl_test_tier name out_var)
    if(${name} MATCHES "^kernel_")
        set(${out_var} "kernel;unit" PARENT_SCOPE)
    elseif(${name} MATCHES "^negative_")
        set(${out_var} "negative" PARENT_SCOPE)
    elseif(${name} MATCHES "^conformance_")
        set(${out_var} "conformance" PARENT_SCOPE)
    elseif(${name} MATCHES "^integration_")
        set(${out_var} "integration" PARENT_SCOPE)
    elseif(${name} MATCHES "^e2e_")
        set(${out_var} "e2e" PARENT_SCOPE)
    elseif(${name} MATCHES "^architecture_")
        set(${out_var} "architecture" PARENT_SCOPE)
    else()
        set(${out_var} "unit" PARENT_SCOPE)
    endif()
endfunction()

# Every test runs under hard resource limits (support/bounded.sh): a file it
# writes is capped and every process it starts has a CPU budget, so a test that
# goes wrong fails instead of filling the disk or outliving an interrupted run.
# The wall-clock bound is CTest's own timeout, set on every test.
set(CPPL_BOUNDED "${CMAKE_CURRENT_SOURCE_DIR}/support/bounded.sh")
set(CPPL_TEST_TIMEOUT 600)

# The prefix a test command that is not itself a bash script runs under. Windows
# has no limit the wrapper could set, so such a test runs there directly; a bash
# script test is wrapped everywhere, since bash is already required for it.
if(WIN32)
    set(CPPL_BOUNDED_PREFIX)
else()
    set(CPPL_BOUNDED_PREFIX bash "${CPPL_BOUNDED}")
endif()

function(cppl_add_unit_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE cppl_testing cppl_project_options)
    add_test(NAME ${name} COMMAND ${CPPL_BOUNDED_PREFIX} $<TARGET_FILE:${name}>)

    cppl_test_tier(${name} tier)
    set_tests_properties(${name} PROPERTIES
        LABELS "${tier}"
        TIMEOUT ${CPPL_TEST_TIMEOUT})
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

cppl_add_unit_test(kernel_definedness_test kernel/definedness_test.cpp)
target_link_libraries(kernel_definedness_test PRIVATE cppl_kernel)

cppl_add_unit_test(kernel_adversarial_test kernel/adversarial_kernel_test.cpp)
target_link_libraries(kernel_adversarial_test PRIVATE cppl_kernel)

cppl_add_unit_test(unit_digest_test unit/digest_test.cpp)
target_link_libraries(unit_digest_test PRIVATE cppl_source)

cppl_add_unit_test(unit_interface_test unit/interface_test.cpp)
target_link_libraries(unit_interface_test PRIVATE cppl_artifact)

cppl_add_unit_test(unit_cross_unit_contracts_test unit/cross_unit_contracts_test.cpp)
target_link_libraries(unit_cross_unit_contracts_test PRIVATE cppl_automation cppl_artifact)

# The crash report has to survive the fault it reports, a stack overflow
# included, so it is exercised in a child process that really overflows.
cppl_add_unit_test(unit_crash_report_test unit/crash_report_test.cpp)
target_link_libraries(unit_crash_report_test PRIVATE cppl_driver)

cppl_add_unit_test(unit_lexer_test unit/lexer_test.cpp)
target_link_libraries(unit_lexer_test PRIVATE cppl_frontend)

cppl_add_unit_test(unit_words_test unit/words_test.cpp)
target_link_libraries(unit_words_test PRIVATE cppl_frontend)
target_compile_definitions(unit_words_test PRIVATE
    CPPL_TEST_SPEC="${PROJECT_SOURCE_DIR}/docs/SPEC.md")

cppl_add_unit_test(unit_recognizer_test unit/recognizer_test.cpp)
target_link_libraries(unit_recognizer_test PRIVATE cppl_frontend cppl_diagnostics)

cppl_add_unit_test(unit_draft_recognition_test unit/draft_recognition_test.cpp)
target_link_libraries(unit_draft_recognition_test PRIVATE cppl_frontend cppl_diagnostics)

cppl_add_unit_test(unit_draft_authority_test unit/draft_authority_test.cpp)
target_link_libraries(unit_draft_authority_test PRIVATE cppl_frontend cppl_elaboration cppl_diagnostics)

cppl_add_unit_test(unit_projection_test unit/projection_test.cpp)
target_link_libraries(unit_projection_test PRIVATE cppl_frontend cppl_erasure cppl_diagnostics)
target_compile_definitions(unit_projection_test PRIVATE
    CPPL_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

cppl_add_unit_test(unit_frontend_property_test unit/frontend_property_test.cpp)
target_link_libraries(unit_frontend_property_test PRIVATE cppl_frontend cppl_erasure cppl_diagnostics)

cppl_add_unit_test(unit_vir_test unit/vir_test.cpp)
target_link_libraries(unit_vir_test PRIVATE cppl_vir)

cppl_add_unit_test(unit_verdict_test unit/verdict_test.cpp)
target_link_libraries(unit_verdict_test PRIVATE cppl_kernel cppl_obligations)

cppl_add_unit_test(unit_trust_closure_test unit/trust_closure_test.cpp)
target_link_libraries(unit_trust_closure_test PRIVATE cppl_kernel cppl_obligations)

cppl_add_unit_test(unit_contracts_test unit/contracts_test.cpp)
target_link_libraries(unit_contracts_test PRIVATE cppl_automation)
# Composition's search budget carries its own contract, so the test states it
# directly rather than only through the compiles that happen to exercise it.
target_include_directories(unit_contracts_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../compiler/automation/src)

cppl_add_unit_test(unit_arithmetic_test unit/arithmetic_test.cpp)
target_link_libraries(unit_arithmetic_test PRIVATE cppl_automation)

cppl_add_unit_test(unit_definedness_arithmetic_test unit/definedness_arithmetic_test.cpp)
target_link_libraries(unit_definedness_arithmetic_test PRIVATE cppl_automation)

cppl_add_unit_test(unit_formal_equality_test unit/formal_equality_test.cpp)
target_link_libraries(unit_formal_equality_test PRIVATE cppl_automation)

cppl_add_unit_test(unit_quantified_propositions_test unit/quantified_propositions_test.cpp)
target_link_libraries(unit_quantified_propositions_test PRIVATE cppl_automation)
# The generic case engine, then one suite per provider for its state model.
cppl_add_unit_test(unit_decomposition_test unit/decomposition_test.cpp)
target_link_libraries(unit_decomposition_test PRIVATE cppl_obligations)

cppl_add_unit_test(unit_contradiction_test unit/contradiction_test.cpp)
target_link_libraries(unit_contradiction_test PRIVATE cppl_automation)

cppl_add_unit_test(unit_scoped_enum_provider_test unit/scoped_enum_provider_test.cpp)
target_link_libraries(unit_scoped_enum_provider_test PRIVATE cppl_decomposition)

# cppl-lsp: the language server library, exercised directly rather than
# through a real subprocess/stdio pipe (ARCHITECTURE.md 82's "unit" tier).
cppl_add_unit_test(lsp_document_test unit/lsp_document_test.cpp)
target_link_libraries(lsp_document_test PRIVATE cppl_lsp)

cppl_add_unit_test(lsp_decomposition_view_test unit/lsp_decomposition_view_test.cpp)
target_link_libraries(lsp_decomposition_view_test PRIVATE cppl_lsp)

cppl_add_unit_test(lsp_linter_test unit/lsp_linter_test.cpp)
target_link_libraries(lsp_linter_test PRIVATE cppl_lsp)

cppl_add_unit_test(lsp_position_test unit/lsp_position_test.cpp)
target_link_libraries(lsp_position_test PRIVATE cppl_lsp)

cppl_add_unit_test(lsp_server_test unit/lsp_server_test.cpp)
target_link_libraries(lsp_server_test PRIVATE cppl_lsp)
# Compiles documents that include fixture headers, as an editor would.
target_compile_definitions(lsp_server_test PRIVATE
    CPPL_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

cppl_add_unit_test(lsp_uri_test unit/lsp_uri_test.cpp)
target_link_libraries(lsp_uri_test PRIVATE cppl_lsp)

cppl_add_unit_test(lsp_json_test unit/lsp_json_test.cpp)
target_link_libraries(lsp_json_test PRIVATE cppl_lsp)

cppl_add_unit_test(lsp_transport_test unit/lsp_transport_test.cpp)
target_link_libraries(lsp_transport_test PRIVATE cppl_lsp)

cppl_add_unit_test(lsp_framing_test unit/lsp_framing_test.cpp)
target_link_libraries(lsp_framing_test PRIVATE cppl_lsp)

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

# Semantic tokens, including the server's use of the real compile to decide
# which `contradiction` statements the translation unit reads as claims.
cppl_add_unit_test(lsp_semantic_tokens_test unit/lsp_semantic_tokens_test.cpp)
target_link_libraries(lsp_semantic_tokens_test PRIVATE cppl_lsp)
target_compile_definitions(lsp_semantic_tokens_test PRIVATE
    CPPL_TEST_FIXTURES_DIR="${CPPL_TEST_FIXTURES}"
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

# Navigation, answered by Clang over each document's projection, including
# headers under the fixtures' include directory.
cppl_add_unit_test(lsp_navigation_test unit/lsp_navigation_test.cpp)
target_link_libraries(lsp_navigation_test PRIVATE cppl_lsp)
target_compile_definitions(lsp_navigation_test PRIVATE
    CPPL_TEST_FIXTURES_DIR="${CPPL_TEST_FIXTURES}"
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

cppl_add_unit_test(lsp_references_test unit/lsp_references_test.cpp)
target_link_libraries(lsp_references_test PRIVATE cppl_lsp)
target_compile_definitions(lsp_references_test PRIVATE
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

# Navigation by the names proof statements use, from what the compiler resolved.
cppl_add_unit_test(lsp_proof_names_test unit/lsp_proof_names_test.cpp)
target_link_libraries(lsp_proof_names_test PRIVATE cppl_lsp)
target_compile_definitions(lsp_proof_names_test PRIVATE
    CPPL_TEST_FIXTURES_DIR="${CPPL_TEST_FIXTURES}"
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

cppl_add_unit_test(lsp_hover_test unit/lsp_hover_test.cpp)
target_link_libraries(lsp_hover_test PRIVATE cppl_lsp)
target_compile_definitions(lsp_hover_test PRIVATE
    CPPL_TEST_FIXTURES_DIR="${CPPL_TEST_FIXTURES}"
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

# Verification status, copied from the kernel's verdicts into lenses and hover.
cppl_add_unit_test(lsp_verification_test unit/lsp_verification_test.cpp)
target_link_libraries(lsp_verification_test PRIVATE cppl_lsp)
target_compile_definitions(lsp_verification_test PRIVATE
    CPPL_TEST_FIXTURES_DIR="${CPPL_TEST_FIXTURES}"
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

cppl_add_unit_test(lsp_completion_test unit/lsp_completion_test.cpp)
target_link_libraries(lsp_completion_test PRIVATE cppl_lsp)
target_compile_definitions(lsp_completion_test PRIVATE
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

cppl_add_unit_test(lsp_signature_help_test unit/lsp_signature_help_test.cpp)
target_link_libraries(lsp_signature_help_test PRIVATE cppl_lsp)
target_compile_definitions(lsp_signature_help_test PRIVATE
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

cppl_add_unit_test(lsp_symbols_test unit/lsp_symbols_test.cpp)
target_link_libraries(lsp_symbols_test PRIVATE cppl_lsp)
target_compile_definitions(lsp_symbols_test PRIVATE
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

cppl_add_unit_test(lsp_compile_commands_test unit/lsp_compile_commands_test.cpp)
target_link_libraries(lsp_compile_commands_test PRIVATE cppl_lsp)
target_compile_definitions(lsp_compile_commands_test PRIVATE
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

cppl_add_unit_test(lsp_background_test unit/lsp_background_test.cpp)
target_link_libraries(lsp_background_test PRIVATE cppl_lsp)
target_compile_definitions(lsp_background_test PRIVATE
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

cppl_add_unit_test(lsp_inlay_hints_test unit/lsp_inlay_hints_test.cpp)
target_link_libraries(lsp_inlay_hints_test PRIVATE cppl_lsp)
target_compile_definitions(lsp_inlay_hints_test PRIVATE
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

cppl_add_unit_test(lsp_structure_test unit/lsp_structure_test.cpp)
target_link_libraries(lsp_structure_test PRIVATE cppl_lsp)
target_compile_definitions(lsp_structure_test PRIVATE
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

cppl_add_unit_test(lsp_workspace_index_test unit/lsp_workspace_index_test.cpp)
target_link_libraries(lsp_workspace_index_test PRIVATE cppl_lsp)
target_compile_definitions(lsp_workspace_index_test PRIVATE
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

cppl_add_unit_test(lsp_rename_test unit/lsp_rename_test.cpp)
target_link_libraries(lsp_rename_test PRIVATE cppl_lsp)
target_compile_definitions(lsp_rename_test PRIVATE
    CPPL_TEST_DEFAULT_CLANG="${CPPL_DEFAULT_CLANG}")

# Driver-level suites invoke the real compiler on real fixtures and write what
# it produces into the work tree, so each one runs under the same limits.
function(cppl_add_script_test name script)
    add_test(NAME ${name}
             COMMAND bash "${CPPL_BOUNDED}" bash "${CMAKE_CURRENT_SOURCE_DIR}/${script}"
                     $<TARGET_FILE:cppl> "${CPPL_TEST_FIXTURES}" "${CPPL_TEST_WORK}"
                     "${CPPL_DEFAULT_CLANG}")

    cppl_test_tier(${name} tier)
    set_tests_properties(${name} PROPERTIES
        LABELS "${tier};script"
        TIMEOUT ${CPPL_TEST_TIMEOUT})
endfunction()

# cppl-format: the CLI surface of compiler/formatter (--check/-i), exercised
# as an installed command exactly as a developer or CI would invoke it.
add_test(NAME format_check
         COMMAND bash "${CPPL_BOUNDED}" bash "${CMAKE_CURRENT_SOURCE_DIR}/format/format_check.sh"
                 $<TARGET_FILE:cppl-format> "${CPPL_TEST_FIXTURES}" "${CPPL_TEST_WORK}")
set_tests_properties(format_check PROPERTIES
    LABELS "format;script"
    TIMEOUT ${CPPL_TEST_TIMEOUT})

cppl_add_script_test(conformance_ordinary_cpp conformance/ordinary_cpp.sh)
cppl_add_script_test(conformance_contextual_identifiers conformance/contextual_identifiers.sh)
cppl_add_script_test(conformance_target_standard_isolation conformance/target_standard_isolation.sh)

# The documented examples are compiled by the compiler they document.
add_test(NAME conformance_readme_examples
         COMMAND bash "${CPPL_BOUNDED}" bash "${CMAKE_CURRENT_SOURCE_DIR}/conformance/readme_examples.sh"
                 $<TARGET_FILE:cppl> "${CMAKE_SOURCE_DIR}/README.md" "${CPPL_TEST_WORK}")
set_tests_properties(conformance_readme_examples PROPERTIES
    LABELS "conformance;script"
    TIMEOUT ${CPPL_TEST_TIMEOUT})
add_test(NAME conformance_guide_examples
         COMMAND bash "${CPPL_BOUNDED}" bash "${CMAKE_CURRENT_SOURCE_DIR}/conformance/guide_examples.sh"
                 $<TARGET_FILE:cppl> "${CMAKE_SOURCE_DIR}/docs/DEVELOPER_GUIDE.md" "${CPPL_TEST_WORK}")
set_tests_properties(conformance_guide_examples PROPERTIES
    LABELS "conformance;script"
    TIMEOUT ${CPPL_TEST_TIMEOUT})

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
cppl_add_script_test(e2e_memory_capabilities e2e/memory_capabilities.sh)
cppl_add_script_test(negative_memory_capabilities negative/memory_capabilities.sh)
cppl_add_script_test(e2e_refinement_types e2e/refinement_types.sh)
cppl_add_script_test(e2e_refinement_flow e2e/refinement_flow.sh)
cppl_add_script_test(e2e_verified_templates e2e/verified_templates.sh)
cppl_add_script_test(negative_template_identity negative/template_identity.sh)
cppl_add_script_test(e2e_trusted_assumptions e2e/trusted_assumptions.sh)
cppl_add_script_test(negative_refinement_types negative/refinement_types.sh)
cppl_add_script_test(e2e_enum_cases e2e/enum_cases.sh)
cppl_add_script_test(negative_proof_cases negative/proof_cases.sh)
cppl_add_script_test(e2e_structural_cases e2e/structural_cases.sh)
cppl_add_script_test(negative_structural_cases negative/structural_cases.sh)
cppl_add_script_test(negative_case_providers negative/case_providers.sh)
cppl_add_script_test(e2e_case_split e2e/case_split.sh)
cppl_add_script_test(negative_case_splits negative/case_splits.sh)
cppl_add_script_test(e2e_instantiated_proof e2e/instantiated_proof.sh)
cppl_add_script_test(e2e_conditional_proof e2e/conditional_proof.sh)
cppl_add_script_test(e2e_contradiction e2e/contradiction.sh)
cppl_add_script_test(e2e_omitted_case e2e/omitted_case.sh)
cppl_add_script_test(e2e_trust_closure e2e/trust_closure.sh)
cppl_add_script_test(e2e_unsafe_boundary e2e/unsafe_boundary.sh)
cppl_add_script_test(e2e_ghost_state e2e/ghost_state.sh)
cppl_add_script_test(e2e_termination e2e/termination.sh)
cppl_add_script_test(negative_termination negative/termination.sh)
cppl_add_script_test(negative_ghost_state negative/ghost_state.sh)
cppl_add_script_test(negative_unsafe_boundary negative/unsafe_boundary.sh)
cppl_add_script_test(negative_contradictions negative/contradictions.sh)
cppl_add_script_test(e2e_impossible_path e2e/impossible_path.sh)
cppl_add_script_test(negative_impossible_paths negative/impossible_paths.sh)
cppl_add_script_test(negative_trusted_dependencies negative/trusted_dependencies.sh)
cppl_add_script_test(e2e_rewritten_proof e2e/rewritten_proof.sh)
cppl_add_script_test(e2e_verified_functions e2e/verified_functions.sh)
cppl_add_script_test(e2e_verified_calls e2e/verified_calls.sh)
cppl_add_script_test(e2e_verified_paths e2e/verified_paths.sh)
cppl_add_script_test(e2e_verified_locals e2e/verified_locals.sh)
cppl_add_script_test(e2e_verified_arithmetic e2e/verified_arithmetic.sh)
cppl_add_script_test(e2e_verified_loops e2e/verified_loops.sh)
cppl_add_script_test(e2e_verified_methods e2e/verified_methods.sh)

# Contracts across translation units, through verification interfaces
# (SPEC.md XTU-*, RFC 0017).
cppl_add_script_test(e2e_cross_tu e2e/cross_tu.sh)
cppl_add_script_test(negative_cross_tu negative/cross_tu.sh)

# Erasure against programs written without C++L: runtime behaviour and code,
# native ABI across a translation-unit boundary, and source positions.
cppl_add_script_test(e2e_erasure_equivalence e2e/erasure_equivalence.sh)
cppl_add_script_test(e2e_abi_equivalence e2e/abi_equivalence.sh)
cppl_add_script_test(e2e_erasure_source_mapping e2e/erasure_source_mapping.sh)
cppl_add_script_test(negative_erasure negative/erasure.sh)

cppl_add_script_test(negative_broken_implementation negative/broken_implementation.sh)
cppl_add_script_test(negative_unproven_dependencies negative/unproven_dependencies.sh)
cppl_add_script_test(negative_written_proofs negative/written_proofs.sh)
cppl_add_script_test(negative_refused_declarations negative/refused_declarations.sh)
cppl_add_script_test(negative_verified_functions negative/verified_functions.sh)
cppl_add_script_test(negative_verified_calls negative/verified_calls.sh)
cppl_add_script_test(negative_verified_paths negative/verified_paths.sh)
cppl_add_script_test(negative_verified_locals negative/verified_locals.sh)
cppl_add_script_test(negative_verified_loops negative/verified_loops.sh)
cppl_add_script_test(negative_verified_methods negative/verified_methods.sh)

cppl_add_script_test(integration_header_law integration/header_law.sh)
cppl_add_script_test(integration_source_identity integration/source_identity.sh)
cppl_add_script_test(integration_determinism integration/determinism.sh)
cppl_add_script_test(integration_parallel_verification integration/parallel_verification.sh)
cppl_add_script_test(integration_signalled_compiler integration/signalled_compiler.sh)

add_test(NAME architecture_dependency_boundaries
         COMMAND bash "${CPPL_BOUNDED}" bash "${CMAKE_CURRENT_SOURCE_DIR}/architecture/dependency_boundaries.sh"
                 "${CMAKE_SOURCE_DIR}/kernel" "$<TARGET_FILE:cppl_kernel>")
set_tests_properties(architecture_dependency_boundaries PROPERTIES
    LABELS "architecture;script"
    TIMEOUT ${CPPL_TEST_TIMEOUT})

add_test(NAME architecture_draft_boundaries
         COMMAND bash "${CPPL_BOUNDED}" bash "${CMAKE_CURRENT_SOURCE_DIR}/architecture/draft_boundaries.sh"
                 "${CMAKE_SOURCE_DIR}")
set_tests_properties(architecture_draft_boundaries PROPERTIES
    LABELS "architecture;script"
    TIMEOUT ${CPPL_TEST_TIMEOUT})

# The executables C++L ships carry the mitigations cmake/Hardening.cmake asks
# for. A target that stops linking cppl_project_options would lose them
# without a build error, so the linked files are read. Windows reports its
# mitigations in a form this reader does not parse yet.
if(CPPL_ENABLE_HARDENING AND NOT WIN32)
    cppl_find_llvm_tool(CPPL_LLVM_READOBJ llvm-readobj)
    cppl_find_llvm_tool(CPPL_LLVM_NM llvm-nm)

    add_test(NAME architecture_hardening
             COMMAND bash "${CPPL_BOUNDED}" bash "${CMAKE_CURRENT_SOURCE_DIR}/architecture/hardening.sh"
                     "${CPPL_LLVM_READOBJ}" "${CPPL_LLVM_NM}"
                     $<TARGET_FILE:cppl> $<TARGET_FILE:cppl-lsp> $<TARGET_FILE:cppl-format>
                     $<TARGET_FILE:cppl-spec-rules>)
    set_tests_properties(architecture_hardening PROPERTIES
        LABELS "architecture;script"
        TIMEOUT ${CPPL_TEST_TIMEOUT})
endif()

add_subdirectory(fuzz)

# The repository checks are CMake scripts, so they run under CTest like
# anything else. Registering them here means a plain `ctest` catches a
# committed host path, a broken preset convention or a local CI run that no
# longer runs what the workflow runs, rather than that
# depending on someone remembering to run `make ci-checks` (AGENTS.md 40).
foreach(check IN ITEMS HostPaths PresetLayout WorkflowSteps)
    string(TOLOWER "ci_${check}" check_test)

    add_test(NAME ${check_test}
             COMMAND ${CPPL_BOUNDED_PREFIX} "${CMAKE_COMMAND}" -P
                     "${CMAKE_SOURCE_DIR}/cmake/ci/Check${check}.cmake")

    set_tests_properties(${check_test} PROPERTIES
        LABELS "ci;repository"
        TIMEOUT ${CPPL_TEST_TIMEOUT})
endforeach()

# The editors' shared TextMate grammar decides what is colored as C++L. Its
# regression cases pin the contextual-word boundary (GRAMMAR.md 1): a grammar
# that colored every `result` or `law` would mis-highlight ordinary C++.
find_program(CPPL_NODE_EXECUTABLE NAMES node)
if(CPPL_NODE_EXECUTABLE)
    add_test(NAME editors_grammar
             COMMAND ${CPPL_BOUNDED_PREFIX} "${CPPL_NODE_EXECUTABLE}"
                     "${CMAKE_SOURCE_DIR}/editors/shared/test-grammar.js")
    set_tests_properties(editors_grammar PROPERTIES
        LABELS "editors;repository"
        TIMEOUT ${CPPL_TEST_TIMEOUT})
endif()
