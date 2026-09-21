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

const std::string kUnit = "# 1 \"main.cpp\"\n"
                          "pure int identity(int x) {\n"
                          "    return x;\n"
                          "}\n"
                          "law identity_returns_input(int x)\n"
                          "    proves (identity(x) == x);\n"
                          "proof identity_returns_input_holds(int x)\n"
                          "    proves(identity_returns_input(x))\n"
                          "{\n"
                          "    refl;\n"
                          "}\n"
                          "law identity_of_zero()\n"
                          "    proves (identity(0) == 0);\n"
                          "proof identity_of_zero_holds()\n"
                          "    proves(identity_of_zero())\n"
                          "{\n"
                          "    exact identity_returns_input_holds(0);\n"
                          "}\n"
                          "law identity_under_a_premise(int x)\n"
                          "    expects(identity(x) == 0)\n"
                          "    proves (identity(x) == x);\n"
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

} // namespace

CPPL_TEST(physical_declarations_stay_distinct_when_displayed_locations_repeat) {
    const std::string source = "#line 1 \"same.cpp\"\n"
                               "verified unsigned a() ensures(result == 0u) {return 0u;}"
                               "law same() proves (0u == 0u);\n"
                               "#line 1 \"same.cpp\"\n"
                               "verified unsigned b() ensures(result == 1u) {return 1u;}"
                               "namespace B {law same() proves (1u == 1u);}\n"
                               "#line 5 \"same.cpp\"\n"
                               "pure unsigned c() {return 2u;}\n"
                               "#line 5 \"same.cpp\"\n"
                               "pure unsigned d() {return 3u;}";
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(source, "input.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    const auto projection = cppl::frontend::project(stream, syntax, {});
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(projection.declaration_offsets.size(), 4u);
    CPPL_CHECK_EQ(projection.specification_functions.size(), 2u);
    CPPL_CHECK(projection.specification_functions[0].analysis_offset !=
               projection.specification_functions[1].analysis_offset);
    for (const auto& declaration : projection.declaration_offsets) {
        CPPL_CHECK_EQ(source[declaration.original], projection.analysis[declaration.analysis]);
        CPPL_CHECK_EQ(projection.declaration_offset(declaration.original), declaration.analysis);
    }
    for (const auto& law : projection.specification_functions) {
        CPPL_CHECK_EQ(projection.analysis.substr(law.analysis_offset, law.name.size()), law.name);
    }
    CPPL_CHECK(!projection.declaration_offset(source.size()).has_value());
    const auto erased = cppl::erasure::erase(stream, syntax, projection, engine);
    CPPL_CHECK(erased.report.only_deletions);
    CPPL_CHECK(erased.report.lines_preserved);
    CPPL_CHECK(!engine.has_errors());
}

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

    const cppl::frontend::TokenStream runtime = cppl::frontend::lex(projection.runtime, "main.cpp");

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

    const cppl::frontend::TokenStream runtime = cppl::frontend::lex(projection.runtime, "main.cpp");
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
        CPPL_CHECK(token.text != "rewrite");
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
    CPPL_CHECK_EQ(projection.specification_functions[0].name, std::string("identity_returns_input"));
    CPPL_CHECK(projection.analysis.find("bool identity_returns_input(int x)") != std::string::npos);
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
    const std::size_t emitted = projection.analysis.find("static decltype(auto) " + argument + "()");
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
    CPPL_CHECK(projection.analysis.find("static decltype(auto) " + assumed + "(int x)") != std::string::npos);

    // The statement itself stays C++L: what reaches Clang is the proposition
    // the statement names, never the statement.
    const cppl::frontend::TokenStream analysis = cppl::frontend::lex(projection.analysis, "main.cpp");
    for (const cppl::frontend::Token& token : analysis.tokens()) {
        CPPL_CHECK(token.text != "assume");
    }
}

CPPL_TEST(formal_connectives_are_recorded_with_specification_precedence) {
    // Equivalence is looser than implication, which is looser than `&&`, and a
    // chain of equivalences groups to the left (GRAMMAR.md 30, 33). Classical
    // logic cannot tell the two groupings of a chain apart, so the recorded
    // shape is what pins which one the projector built.
    const std::string source =
        "# 1 \"main.cpp\"\n"
        "law loosest(unsigned x)\n"
        "    proves (Eq<unsigned>(x, x) && Eq<unsigned>(x, x) -> Eq<unsigned>(x, x) <-> Eq<unsigned>(x, x));\n"
        "law chained(unsigned x)\n"
        "    proves (Eq<unsigned>(x, x) <-> Eq<unsigned>(x, x) <-> Eq<unsigned>(x, x));\n"
        "law between(unsigned x)\n"
        "    proves (Eq<unsigned>(x, x) && Eq<unsigned>(x, x) || Eq<unsigned>(x, x) -> Eq<unsigned>(x, x));\n"
        "int main() { return 0; }\n";

    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(source, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    CPPL_CHECK(projection.diagnostics.empty());
    CPPL_CHECK_EQ(projection.proposition_probes.size(), std::size_t{3});

    using Kind = cppl::source::ProjectionKind;
    const cppl::source::ProjectionShape& loosest = projection.proposition_probes[0].shape;
    CPPL_CHECK(loosest.kind == Kind::Equivalence);
    CPPL_CHECK_EQ(loosest.children.size(), std::size_t{2});
    CPPL_CHECK(loosest.children[0].kind == Kind::Implication);
    CPPL_CHECK(loosest.children[0].children[0].kind == Kind::Conjunction);
    CPPL_CHECK(loosest.children[0].children[1].kind == Kind::Equality);
    CPPL_CHECK(loosest.children[1].kind == Kind::Equality);

    const cppl::source::ProjectionShape& chained = projection.proposition_probes[1].shape;
    CPPL_CHECK(chained.kind == Kind::Equivalence);
    CPPL_CHECK(chained.children[0].kind == Kind::Equivalence);
    CPPL_CHECK(chained.children[1].kind == Kind::Equality);

    // `||` stands between them: looser than `&&`, tighter than `->`.
    const cppl::source::ProjectionShape& between = projection.proposition_probes[2].shape;
    CPPL_CHECK(between.kind == Kind::Implication);
    CPPL_CHECK(between.children[0].kind == Kind::Disjunction);
    CPPL_CHECK(between.children[0].children[0].kind == Kind::Conjunction);
    CPPL_CHECK(between.children[0].children[1].kind == Kind::Equality);
    CPPL_CHECK(between.children[1].kind == Kind::Equality);
}

CPPL_TEST(erasure_reports_the_properties_it_checked) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    const cppl::erasure::Erased erased = cppl::erasure::erase(stream, syntax, projection, engine);

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

CPPL_TEST(contracts_erase_without_changing_runtime_values_or_source_locations) {
    const std::string text = "verified unsigned f(unsigned x)\n"
                             "expects(x == 0u)\nensures(result == 0u)\n{ return x; } "
                             "verified unsigned g(unsigned y) ensures(result == y) { return y; }\n"
                             "int result = 7;\n";
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(text, "contracts.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    const auto projection = cppl::frontend::project(stream, syntax, {});
    const auto erased = cppl::erasure::erase(stream, syntax, projection, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK(erased.report.only_deletions);
    CPPL_CHECK(erased.report.lines_preserved);
    CPPL_CHECK_EQ(projection.contract_functions.size(), std::size_t{2});
    CPPL_CHECK(projection.runtime.find("int result = 7;") != std::string::npos);
    CPPL_CHECK(projection.runtime.find("return x;") != std::string::npos);
    CPPL_CHECK(projection.runtime.find("return y;") != std::string::npos);

    const auto analysis = cppl::frontend::lex(projection.analysis, "contracts.cpp");
    for (const auto& function : syntax.verified_functions) {
        bool found = false;
        for (const auto& token : analysis.tokens()) {
            if (token.text == function.function_name) {
                const auto location = analysis.location_of(token);
                CPPL_CHECK_EQ(location.line, function.function_location.line);
                CPPL_CHECK_EQ(location.column, function.function_location.column);
                found = true;
            }
        }
        CPPL_CHECK(found);
    }
}

CPPL_TEST(loop_invariants_leave_the_runtime_and_reach_clang_inside_the_body) {
    const std::string text = "verified unsigned f(unsigned n) ensures(result == n) {\n"
                             "    unsigned i = 0u;\n"
                             "    while (i < n)\n"
                             "        invariant(i <= n)\n"
                             "        invariant(i >= 0u) { i = i + 1u; }\n"
                             "    return i;\n"
                             "}\n";
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(text, "loops.cpp");
    const auto syntax = cppl::frontend::recognize(stream, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(syntax.loops.size(), std::size_t{1});
    CPPL_CHECK_EQ(syntax.loops[0].invariants.size(), std::size_t{2});
    const auto projection = cppl::frontend::project(stream, syntax, {});
    const auto erased = cppl::erasure::erase(stream, syntax, projection, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK(erased.report.only_deletions);
    CPPL_CHECK(erased.report.lines_preserved);
    CPPL_CHECK(projection.runtime.find("invariant") == std::string::npos);
    CPPL_CHECK(projection.runtime.find("while (i < n)") != std::string::npos);
    CPPL_CHECK(projection.runtime.find("{ i = i + 1u; }") != std::string::npos);
    CPPL_CHECK_EQ(projection.loop_invariants.size(), std::size_t{2});
    for (const auto& marker : projection.loop_invariants) {
        CPPL_CHECK(projection.analysis.find("bool " + marker.name + " = (") != std::string::npos);
    }
    // The body's own statements keep their line and column in the analysis.
    const auto analysis = cppl::frontend::lex(projection.analysis, "loops.cpp");
    bool found = false;
    for (const auto& token : analysis.tokens()) {
        if (token.text == "i" && analysis.location_of(token).line == 5 && analysis.location_of(token).column == 30) {
            found = true;
        }
    }
    CPPL_CHECK(found);
}
