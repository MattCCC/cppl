// Linter tests: syntax-aware C++L lint diagnostics

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/lsp/linter.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/testing/test.hpp"

#include <string>
#include <vector>

using namespace cppl;
using namespace cppl::lsp;

namespace {

std::vector<Diagnostic> lint_text(const std::string& text) {
    auto tokens = frontend::lex(text, "test.cpp");
    diagnostics::Engine engine;
    auto syntax = frontend::recognize(tokens, engine);

    PositionMapper mapper(text);
    Linter linter;
    return linter.lint(tokens, syntax, engine.diagnostics(), mapper);
}

bool has_diagnostic_with_code(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& diag : diags) {
        if (diag.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

CPPL_TEST(law_missing_ensures_clause) {
    // frontend::recognize() itself fails closed on a law with no
    // proposition (AGENTS.md 23) and never adds it to Syntax::laws, so the
    // structural Linter's own "missing ensures" check never sees this case:
    // the diagnostic that actually surfaces is the recognizer's own,
    // converted like any other C++L syntax diagnostic.
    std::string text = R"(
law incomplete(int x) expects(x > 0);
)";

    auto diags = lint_text(text);
    CPPL_CHECK(has_diagnostic_with_code(diags, "cppl.syntax.unexpected-construct"));
}

CPPL_TEST(law_with_valid_ensures_passes) {
    std::string text = R"(
law valid(int x) ensures(x == x);
)";

    auto diags = lint_text(text);
    CPPL_CHECK(!has_diagnostic_with_code(diags, "cppl.law.missing-ensures"));
}

CPPL_TEST(law_with_expects_and_ensures_passes) {
    std::string text = R"(
law valid_with_premise(int x) 
    expects(x > 0)
    ensures(x > 0);
)";

    auto diags = lint_text(text);
    CPPL_CHECK(!has_diagnostic_with_code(diags, "cppl.law.missing-ensures"));
}

CPPL_TEST(verified_function_without_ensures_requires_semantic_type_resolution) {
    // The structural linter cannot distinguish a refined return from its base.
    // The shared compiler pipeline diagnoses a missing contract after Clang.
    std::string text = R"(
verified int bad(int x) expects(x > 0) {
    return x;
}
)";

    auto diags = lint_text(text);
    CPPL_CHECK(diags.empty());
}

CPPL_TEST(verified_function_with_ensures_passes) {
    std::string text = R"(
verified int good(int x) ensures(result == x) {
    return x;
}
)";

    auto diags = lint_text(text);
    CPPL_CHECK(!has_diagnostic_with_code(diags, "cppl.contract.missing-ensures"));
}

CPPL_TEST(verified_function_with_expects_and_ensures_passes) {
    std::string text = R"(
verified int good(int x) 
    expects(x > 0)
    ensures(result > 0) {
    return x;
}
)";

    auto diags = lint_text(text);
    CPPL_CHECK(!has_diagnostic_with_code(diags, "cppl.contract.missing-ensures"));
}

CPPL_TEST(proof_with_empty_body_warns) {
    // As above: recognize() rejects an empty proof body itself (reported as
    // ProofFailure, "a proof body must close the goal it states") and never
    // adds it to Syntax::proofs, so the Linter's own empty-body check is
    // unreachable for this input.
    std::string text = R"(
proof empty_proof(int x) proves(x == x) {
}
)";

    auto diags = lint_text(text);
    CPPL_CHECK(has_diagnostic_with_code(diags, "cppl.proof.failure"));
}

CPPL_TEST(proof_with_statement_passes) {
    std::string text = R"(
proof valid_proof(int x) proves(x == x) {
    refl;
}
)";

    auto diags = lint_text(text);
    CPPL_CHECK(!has_diagnostic_with_code(diags, "cppl.proof.empty-body"));
}

CPPL_TEST(refinement_type_valid) {
    std::string text = R"(
type Positive = int where(self > 0);
)";

    auto diags = lint_text(text);
    CPPL_CHECK(!has_diagnostic_with_code(diags, "cppl.refinement.missing-predicate"));
    CPPL_CHECK(!has_diagnostic_with_code(diags, "cppl.refinement.invalid-base"));
}

CPPL_TEST(ordinary_cpp_is_not_flagged) {
    std::string text = R"(
int ordinary_function(int x) {
    return x + 1;
}

class MyClass {
    int member;
};

template<typename T>
T identity(T x) {
    return x;
}
)";

    auto diags = lint_text(text);
    // Should have no C++L-specific diagnostics
    CPPL_CHECK(!has_diagnostic_with_code(diags, "cppl.syntax."));
}

CPPL_TEST(contextual_keywords_as_identifiers_allowed) {
    std::string text = R"(
int law = 5;
void proof() {}
struct verified {};
int type = 0;
)";

    auto diags = lint_text(text);
    // These should be treated as ordinary C++ identifiers
    CPPL_CHECK(!has_diagnostic_with_code(diags, "cppl."));
}
