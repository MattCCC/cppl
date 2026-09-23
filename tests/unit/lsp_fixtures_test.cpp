// Fixture-driven integration tests for the LSP buffer-compile pipeline.
//
// This is the primary acceptance invariant from the cppl-lsp task spec: a
// valid C++L fixture must not receive bogus Clang diagnostics solely
// because it contains valid C++L syntax, and a genuine problem (a C++L
// syntax error, or a real ordinary-C++ type error) must still surface.
// These tests drive cppl::driver::compile_buffer directly, the same
// library call Server::publish_diagnostics makes, against real files under
// tests/fixtures/ -- the corpus the task requires the LSP to prove itself
// against -- rather than a subprocess/JSON-RPC round trip, to keep the
// suite fast and free of transport flakiness.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/driver/buffer_compile.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <fstream>
#include <functional>
#include <ios>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace cppl;

namespace {

std::string read_fixture(const std::string& name) {
    const std::string path = std::string(CPPL_TEST_FIXTURES_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        ::cppl::testing::fail(__FILE__, __LINE__, "could not read fixture '" + path + "'");
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

driver::BufferCompileOutcome compile_fixture(const std::string& name, diagnostics::Engine& engine,
                                             const std::vector<std::string>& extra_arguments = {},
                                             const std::string& standard = "-std=c++20") {
    driver::BufferCompileRequest request;
    request.virtual_path = std::string(CPPL_TEST_FIXTURES_DIR) + "/" + name;
    request.text = read_fixture(name);
    request.clang = CPPL_TEST_DEFAULT_CLANG;
    request.clang_arguments = extra_arguments;
    request.clang_arguments.push_back(standard);
    // Fixtures resolve #include relative to the fixtures directory
    // (verified_functions.cpp includes "include/verified_contract.hpp"),
    // and the buffer is compiled from a scratch copy, so the search path
    // has to be supplied explicitly.
    request.clang_arguments.push_back("-I" + std::string(CPPL_TEST_FIXTURES_DIR));
    return driver::compile_buffer(request, engine);
}

bool has_category(const diagnostics::Engine& engine, diagnostics::Category category) {
    for (const auto& diagnostic : engine.diagnostics()) {
        if (diagnostic.category == category) {
            return true;
        }
    }
    return false;
}

std::size_t count_category(const diagnostics::Engine& engine, diagnostics::Category category) {
    std::size_t count = 0;
    for (const auto& diagnostic : engine.diagnostics()) {
        if (diagnostic.category == category) {
            ++count;
        }
    }
    return count;
}

// Whether `part` lies inside `whole`'s characters. Pointers into different
// allocations have no ordering of their own, so the comparison goes through
// std::less_equal, which is total.
bool within(std::string_view part, const std::string& whole) {
    const std::less_equal<> before;
    return before(whole.data(), part.data()) && before(part.data() + part.size(), whole.data() + whole.size());
}

} // namespace

// --- ownership: what the outcome hands back stays valid ----------------

CPPL_TEST(the_outcome_owns_the_text_its_tokens_refer_to) {
    // The token stream and syntax a buffer compile returns refer into the
    // preprocessed text rather than copying it, and the server reads them
    // after `compile_buffer` has returned, to lint and style-check them. The
    // outcome therefore owns that text, somewhere moving the outcome cannot
    // move it. A token stream pointing into the call's own local was a
    // heap-use-after-free that AddressSanitizer found in `lsp_formatting_test`.
    diagnostics::Engine engine;
    driver::BufferCompileOutcome outcome = compile_fixture("omitted_case.cpp", engine);
    CPPL_CHECK(outcome.tokens != nullptr);
    CPPL_CHECK(outcome.text != nullptr);
    if (outcome.tokens == nullptr || outcome.text == nullptr) {
        return;
    }
    CPPL_CHECK(!outcome.tokens->tokens().empty());
    CPPL_CHECK(within(outcome.tokens->text(), *outcome.text));

    // Moved, as a caller returning it would move it, every view still lands in
    // the text the outcome owns. The end-of-file token's view is empty and
    // refers to no characters at all, so only views that span some are asked.
    const driver::BufferCompileOutcome moved = std::move(outcome);
    CPPL_CHECK(within(moved.tokens->text(), *moved.text));
    std::size_t spanning = 0;
    for (const frontend::Token& token : moved.tokens->tokens()) {
        if (token.text.empty()) {
            continue;
        }
        ++spanning;
        CPPL_CHECK(within(token.text, *moved.text));
    }
    CPPL_CHECK(spanning > 0);
}

// --- valid: the primary acceptance invariant --------------------------

CPPL_TEST(valid_identity_law_produces_no_cpp_semantic_diagnostics) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("identity_law.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK_EQ(count_category(engine, diagnostics::Category::CppSemantic), 0u);
}

CPPL_TEST(valid_verified_functions_produces_no_cpp_semantic_diagnostics) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("verified_functions.cpp", engine, {}, "-std=c++17");
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK_EQ(count_category(engine, diagnostics::Category::CppSemantic), 0u);
}

CPPL_TEST(valid_refinement_types_produces_no_cpp_semantic_diagnostics) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("refinement_types.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK_EQ(count_category(engine, diagnostics::Category::CppSemantic), 0u);
}

CPPL_TEST(valid_verified_storage_uses_the_shared_semantic_bridge) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("verified_storage.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK(!engine.has_errors());
}

CPPL_TEST(possible_reference_call_alias_does_not_retain_an_editor_fact) {
    diagnostics::Engine engine;
    driver::BufferCompileRequest request;
    request.virtual_path = "reference_call_alias.cpp";
    request.text = "verified void zero(int& x) ensures(x == 0) { x = 0; }\n"
                   "verified int wrong(int& x, const int& y) expects(y > 0) ensures(result > 0) {\n"
                   "zero(x); return y; }\n";
    request.clang = CPPL_TEST_DEFAULT_CLANG;
    request.clang_arguments = {"-std=c++20"};
    const auto outcome = driver::compile_buffer(request, engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(engine.has_errors());
    CPPL_CHECK_EQ(count_category(engine, diagnostics::Category::CppSemantic), 0u);
}

CPPL_TEST(valid_written_proof_produces_no_cpp_semantic_diagnostics) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("written_proof.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK_EQ(count_category(engine, diagnostics::Category::CppSemantic), 0u);
}

CPPL_TEST(valid_true_arithmetic_law_produces_no_cpp_semantic_diagnostics) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("true_arithmetic_law.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK_EQ(count_category(engine, diagnostics::Category::CppSemantic), 0u);
}

// structural_cases.cpp exercises every structural decomposition provider
// (variant, optional, tuple, array, struct/product) except std::expected,
// which needs its own feature-gated test below (tests/e2e/structural_cases.sh
// runs it across c++17/c++20/c++23; c++20 alone is enough to prove the LSP
// pipeline doesn't introduce bogus diagnostics of its own).
CPPL_TEST(valid_structural_cases_produces_no_cpp_semantic_diagnostics) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("structural_cases.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK_EQ(count_category(engine, diagnostics::Category::CppSemantic), 0u);
}

// The states the compiler recorded are what completion and hover answer from,
// so the record has to come from the real pipeline rather than a fixture of
// its own. These check the record the compiler actually produced;
// lsp_decomposition_view_test covers how a record becomes completion items.
CPPL_TEST(structural_cases_records_provider_states_for_editors) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("structural_cases.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(!outcome.subject_states.empty());

    // A record with no states would silently offer nothing; that is a bug, not
    // an empty answer.
    for (const auto& record : outcome.subject_states) {
        CPPL_CHECK(!record.provider.empty());
        CPPL_CHECK(!record.representation.empty());
        CPPL_CHECK(!record.states.empty());
    }

    // The residual states must be present and marked. A provider that dropped
    // one would make a proof exhaustive that omits a real runtime state
    // (SPEC.md CASE-004).
    bool saw_valueless = false;
    bool saw_none = false;
    for (const auto& record : outcome.subject_states) {
        for (const auto& state : record.states) {
            if (state.label == "valueless") {
                CPPL_CHECK(state.residual);
                saw_valueless = true;
            }
            if (state.label == "none") {
                CPPL_CHECK(state.residual);
                saw_none = true;
            }
        }
    }
    CPPL_CHECK(saw_valueless);
    CPPL_CHECK(saw_none);

    // Records are looked up by source location, so two statements sharing one
    // would make completion answer for the wrong subject. The fixture nests
    // `cases` four deep, which is where a collision would show up first.
    for (std::size_t i = 0; i < outcome.subject_states.size(); ++i) {
        for (std::size_t j = i + 1; j < outcome.subject_states.size(); ++j) {
            CPPL_CHECK(!(outcome.subject_states[i].location == outcome.subject_states[j].location));
        }
    }
}

CPPL_TEST(product_subjects_record_components_not_alternatives) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("structural_cases.cpp", engine);
    CPPL_CHECK(outcome.ok);

    // A product has exactly one `components` state and no residual: it is not
    // a sum and must never be presented as a partition of alternatives
    // (AGENTS.md 39).
    bool saw_product = false;
    for (const auto& record : outcome.subject_states) {
        if (!record.product) {
            continue;
        }
        saw_product = true;
        CPPL_CHECK_EQ(record.states.size(), 1u);
        CPPL_CHECK(record.states[0].label == "components");
        CPPL_CHECK(!record.states[0].residual);
        CPPL_CHECK(!record.states[0].binders.empty());
    }
    CPPL_CHECK(saw_product);
}

// std::expected is only available from C++23 onward; probe for it first
// exactly as tests/e2e/structural_cases.sh does, rather than hardcoding
// availability by toolchain or standard-library vendor.
bool has_std_expected() {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("expected_cases.cpp", engine, {}, "-std=c++23");
    return outcome.ok && count_category(engine, diagnostics::Category::CppSemantic) == 0u;
}

CPPL_TEST(valid_expected_cases_produces_no_cpp_semantic_diagnostics_when_available) {
    if (!has_std_expected()) {
        return;
    }
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("expected_cases.cpp", engine, {}, "-std=c++23");
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK_EQ(count_category(engine, diagnostics::Category::CppSemantic), 0u);
}

// --- ordinary C++, no C++L at all --------------------------------------

CPPL_TEST(ordinary_cpp_file_is_not_recognized_as_cppl) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("hello.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(!outcome.has_cppl);
    CPPL_CHECK_EQ(engine.diagnostics().size(), 0u);
}

// --- a genuine C++ type error must still surface -----------------------

CPPL_TEST(genuine_cpp_type_error_alongside_valid_cppl_is_reported) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("cppl_with_cpp_type_error.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK(has_category(engine, diagnostics::Category::CppSemantic));

    bool found_type_error = false;
    for (const auto& diagnostic : engine.diagnostics()) {
        if (diagnostic.category == diagnostics::Category::CppSemantic &&
            diagnostic.message.find("int") != std::string::npos) {
            found_type_error = true;
        }
    }
    CPPL_CHECK(found_type_error);
}

// --- a false Law is a proof failure, not a C++ semantic error ----------

CPPL_TEST(false_law_is_a_proof_failure_not_a_cpp_semantic_error) {
    diagnostics::Engine engine;
    const auto outcome = compile_fixture("false_law.cpp", engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK_EQ(count_category(engine, diagnostics::Category::CppSemantic), 0u);
    CPPL_CHECK(engine.has_errors());
}

// An unproven refinement is a proof failure the editor must see, and it must
// reach the buffer pipeline from the same obligation machinery the CLI uses
// rather than from an editor-only rule (tools/cppl-lsp/README.md). A write
// through a reference alias is the interesting case: the alias names the
// referent's storage, so the write owes the refinement there.
CPPL_TEST(unproven_refinement_through_an_alias_is_a_proof_failure) {
    diagnostics::Engine engine;
    driver::BufferCompileRequest request;
    request.virtual_path = "refinement_alias.cpp";
    request.text = "type Positive = int where(self > 0);\n"
                   "verified int wrong() ensures(result > 0) {\n"
                   "    Positive x = 1;\n"
                   "    int& r = x;\n"
                   "    r = 0;\n"
                   "    return x;\n"
                   "}\n";
    request.clang = CPPL_TEST_DEFAULT_CLANG;
    request.clang_arguments = {"-std=c++20"};

    const auto outcome = driver::compile_buffer(request, engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(outcome.has_cppl);
    CPPL_CHECK_EQ(count_category(engine, diagnostics::Category::CppSemantic), 0u);
    CPPL_CHECK(engine.has_errors());
}

// --- malformed C++L must produce a CpplSyntax diagnostic ---------------

CPPL_TEST(malformed_law_produces_cppl_syntax_diagnostic) {
    diagnostics::Engine engine;
    driver::BufferCompileRequest request;
    request.virtual_path = "malformed.cpp";
    request.text = "law incomplete(int x) proves (;\n"; // unbalanced: malformed proposition
    request.clang = CPPL_TEST_DEFAULT_CLANG;
    request.clang_arguments = {"-std=c++20"};

    const auto outcome = driver::compile_buffer(request, engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(engine.has_errors());
}

// --- a genuine ordinary-C++ syntax error, no C++L involved -------------

CPPL_TEST(ordinary_cpp_syntax_error_is_reported) {
    diagnostics::Engine engine;
    driver::BufferCompileRequest request;
    request.virtual_path = "broken.cpp";
    request.text = "int main( {\n  return 0;\n}\n"; // missing ')'
    request.clang = CPPL_TEST_DEFAULT_CLANG;
    request.clang_arguments = {"-std=c++20"};

    const auto outcome = driver::compile_buffer(request, engine);
    CPPL_CHECK(outcome.ok);
    CPPL_CHECK(!outcome.has_cppl);
    CPPL_CHECK(engine.has_errors());
}

// --- preprocessing failure must not crash, must produce a diagnostic ---

CPPL_TEST(missing_include_does_not_crash_and_produces_a_diagnostic) {
    diagnostics::Engine engine;
    driver::BufferCompileRequest request;
    request.virtual_path = "missing_include.cpp";
    request.text = "#include \"this_header_does_not_exist_anywhere.hpp\"\nint main() { return 0; }\n";
    request.clang = CPPL_TEST_DEFAULT_CLANG;
    request.clang_arguments = {"-std=c++20"};

    const auto outcome = driver::compile_buffer(request, engine);
    // Preprocessing failure is reported as a diagnostic; the pipeline itself
    // must not crash (AGENTS.md 23, fail closed).
    CPPL_CHECK(!outcome.ok);
    CPPL_CHECK(engine.has_errors());
}

CPPL_TEST(unknown_clang_executable_does_not_crash_and_produces_a_diagnostic) {
    diagnostics::Engine engine;
    driver::BufferCompileRequest request;
    request.virtual_path = "anything.cpp";
    request.text = "int main() { return 0; }\n";
    request.clang = "cppl-lsp-test-clang-that-does-not-exist";
    request.clang_arguments = {"-std=c++20"};

    const auto outcome = driver::compile_buffer(request, engine);
    CPPL_CHECK(!outcome.ok);
    CPPL_CHECK(engine.has_errors());
}
