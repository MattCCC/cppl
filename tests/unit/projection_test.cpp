// Projection and erasure properties.
//
// The runtime program must be the scanned program with C++L-only spans removed
// and nothing else changed. That property is what makes a C++17 target stay
// C++17: erasure can only delete, so it cannot introduce a construct from a
// later standard (COMPATIBILITY.md, TRUST.md 7).

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/erasure/erase.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <string>

namespace {

const std::string kUnit =
    "# 1 \"main.cpp\"\n"
    "pure int identity(int x) {\n"
    "    return x;\n"
    "}\n"
    "law identity_returns_input(int x)\n"
    "    ensures(identity(x) == x);\n"
    "proof identity_returns_input_holds(int x)\n"
    "    proves(identity_returns_input(x))\n"
    "{\n"
    "    refl;\n"
    "}\n"
    "law identity_of_zero()\n"
    "    ensures(identity(0) == 0);\n"
    "proof identity_of_zero_holds()\n"
    "    proves(identity_of_zero())\n"
    "{\n"
    "    exact identity_returns_input_holds(0);\n"
    "}\n"
    "law identity_under_a_premise(int x)\n"
    "    expects(identity(x) == 0)\n"
    "    ensures(identity(x) == x);\n"
    "proof identity_under_a_premise_holds(int x)\n"
    "    proves(identity_under_a_premise(x))\n"
    "{\n"
    "    assume h : identity(x) == 0;\n"
    "    refl;\n"
    "}\n"
    "int main() { return identity(0); }\n";

std::size_t count_newlines(std::string_view text) {
    return static_cast<std::size_t>(std::ranges::count(text, '\n'));
}

}  // namespace

CPPL_TEST(the_runtime_program_only_loses_text) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(projection.runtime.size(), kUnit.size());
    CPPL_CHECK_EQ(count_newlines(projection.runtime), count_newlines(kUnit));

    for (std::size_t offset = 0; offset < kUnit.size(); ++offset) {
        if (projection.runtime[offset] != kUnit[offset]) {
            CPPL_CHECK_EQ(projection.runtime[offset], ' ');
        }
    }
}

CPPL_TEST(every_runtime_token_is_an_original_token_in_its_original_place) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    const cppl::frontend::TokenStream runtime =
        cppl::frontend::lex(projection.runtime, "main.cpp");

    std::size_t original_index = 0;
    for (const cppl::frontend::Token& token : runtime.tokens()) {
        if (token.kind == cppl::frontend::TokenKind::EndOfFile) {
            continue;
        }
        bool matched = false;
        while (original_index < stream.tokens().size()) {
            const cppl::frontend::Token& candidate = stream.tokens()[original_index++];
            if (candidate.span.offset == token.span.offset && candidate.text == token.text &&
                candidate.line == token.line && candidate.column == token.column) {
                matched = true;
                break;
            }
        }
        CPPL_CHECK(matched);
    }
}

CPPL_TEST(the_runtime_program_carries_no_formal_syntax) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    const cppl::frontend::TokenStream runtime =
        cppl::frontend::lex(projection.runtime, "main.cpp");
    for (const cppl::frontend::Token& token : runtime.tokens()) {
        CPPL_CHECK(token.text != "law");
        CPPL_CHECK(token.text != "ensures");
        CPPL_CHECK(token.text != "pure");
        CPPL_CHECK(token.text != "proof");
        CPPL_CHECK(token.text != "proves");
        CPPL_CHECK(token.text != "refl");
        CPPL_CHECK(token.text != "exact");
        CPPL_CHECK(token.text != "expects");
        CPPL_CHECK(token.text != "assume");
    }
}

CPPL_TEST(the_analysis_program_carries_the_proposition_for_clang_to_resolve) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    CPPL_CHECK_EQ(projection.specification_functions.size(), std::size_t{3});
    // A law is projected under its own name, so a proof can name it through
    // ordinary C++ lookup (GRAMMAR.md 46).
    CPPL_CHECK_EQ(projection.specification_functions[0].name,
                  std::string("identity_returns_input"));
    CPPL_CHECK(projection.analysis.find("bool identity_returns_input(int x)") !=
               std::string::npos);
    CPPL_CHECK(projection.analysis.find("identity(x) == x") != std::string::npos);
    CPPL_CHECK(projection.analysis.find("#line") != std::string::npos);
}

CPPL_TEST(the_analysis_program_carries_the_proposition_a_proof_claims) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    CPPL_CHECK_EQ(projection.proof_functions.size(), std::size_t{3});
    const std::string& name = projection.proof_functions[0].name;
    CPPL_CHECK(projection.analysis.find(name) != std::string::npos);
    CPPL_CHECK(projection.analysis.find("identity_returns_input(x)") != std::string::npos);

    // The proof statements are C++L and are never projected into C++.
    CPPL_CHECK(projection.analysis.find("refl") == std::string::npos);
    CPPL_CHECK(projection.analysis.find("exact") == std::string::npos);
}

CPPL_TEST(the_analysis_program_carries_each_term_a_proof_instantiates_at) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    CPPL_CHECK(projection.proof_functions[0].argument_names.empty());
    CPPL_CHECK_EQ(projection.proof_functions[1].argument_names.size(), std::size_t{1});

    // The term is emitted as an ordinary C++ expression for Clang to resolve,
    // with the type it is given deduced from the expression itself.
    const std::string& argument = projection.proof_functions[1].argument_names[0];
    const std::size_t emitted = projection.analysis.find("static auto " + argument + "()");
    CPPL_CHECK(emitted != std::string::npos);
    CPPL_CHECK(projection.analysis.find("return (", emitted) != std::string::npos);
}

CPPL_TEST(the_analysis_program_carries_the_premise_a_law_supposes) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    // Only the law that states one has a premise function, and it is generated
    // rather than named after the law: the law's own name states what it
    // concludes.
    CPPL_CHECK(projection.specification_functions[0].premise_name.empty());
    CPPL_CHECK(projection.specification_functions[1].premise_name.empty());

    const std::string& premise = projection.specification_functions[2].premise_name;
    CPPL_CHECK(!premise.empty());
    CPPL_CHECK(premise != projection.specification_functions[2].name);
    CPPL_CHECK(projection.analysis.find("bool " + premise + "(int x)") != std::string::npos);
}

CPPL_TEST(the_analysis_program_carries_the_proposition_a_statement_assumes) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    CPPL_CHECK(projection.proof_functions[0].assumption_names.empty());
    CPPL_CHECK_EQ(projection.proof_functions[2].assumption_names.size(), std::size_t{1});

    const std::string& assumed = projection.proof_functions[2].assumption_names[0];
    CPPL_CHECK(projection.analysis.find("static auto " + assumed + "(int x)") !=
               std::string::npos);

    // The statement itself stays C++L: what reaches Clang is the proposition
    // the statement names, never the statement.
    const cppl::frontend::TokenStream analysis =
        cppl::frontend::lex(projection.analysis, "main.cpp");
    for (const cppl::frontend::Token& token : analysis.tokens()) {
        CPPL_CHECK(token.text != "assume");
    }
}

CPPL_TEST(erasure_reports_the_properties_it_checked) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    const cppl::erasure::Erased erased =
        cppl::erasure::erase(stream, syntax, projection, engine);

    CPPL_CHECK(erased.report.only_deletions);
    CPPL_CHECK(erased.report.lines_preserved);
    CPPL_CHECK(erased.report.erased_spans == 7);
    CPPL_CHECK(erased.report.erased_bytes > 0);
    CPPL_CHECK(!engine.has_errors());
}

CPPL_TEST(a_unit_without_formal_syntax_is_left_untouched) {
    const std::string ordinary = "int main() { return 0; }\n";

    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(ordinary, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);

    CPPL_CHECK(syntax.empty());

    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});
    CPPL_CHECK_EQ(projection.runtime, ordinary);
    CPPL_CHECK_EQ(projection.analysis, ordinary);
}
