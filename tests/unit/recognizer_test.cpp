// Contextual recognition: a C++L word is only C++L syntax where ordinary C++
// cannot mean it (SPEC.md 3.1, GRAMMAR.md 1).

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

struct Recognized {
    cppl::diagnostics::Engine engine;
    cppl::frontend::Syntax syntax;
};

void recognize(const std::string& text, Recognized& out) {
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(text, "main.cpp");
    out.syntax = cppl::frontend::recognize(stream, out.engine);
}

} // namespace

CPPL_TEST(a_law_declaration_is_recognized) {
    Recognized result;
    recognize("law identity_returns_input(int x)\n    proves (identity(x) == x);\n", result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.laws.size(), std::size_t{1});
    CPPL_CHECK_EQ(result.syntax.laws[0].name, std::string("identity_returns_input"));
    CPPL_CHECK_EQ(result.syntax.laws[0].clauses.size(), std::size_t{1});
    CPPL_CHECK(result.syntax.laws[0].proposition() != nullptr);
}

CPPL_TEST(ordinary_identifiers_named_after_cppl_words_stay_ordinary) {
    Recognized result;
    recognize("int law = 1;\n"
              "void proof() {}\n"
              "struct ghost {};\n"
              "int verified = 0;\n"
              "int pure = 2;\n"
              "law_type trusted;\n",
              result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.empty());
}

CPPL_TEST(a_function_returning_a_type_named_law_is_not_a_law) {
    Recognized result;
    recognize("law make_law(int x);\nlaw other(int x) { return x; }\n", result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.empty());
}

CPPL_TEST(a_declaration_of_a_variable_whose_type_is_named_pure_stays_ordinary) {
    Recognized result;
    recognize("pure value;\npure f(int x);\n", result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.empty());
}

CPPL_TEST(the_pure_specifier_is_attached_to_its_function) {
    Recognized result;
    recognize("pure int identity(int x) {\n    return x;\n}\n", result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.pure_markers.size(), std::size_t{1});
    CPPL_CHECK_EQ(result.syntax.pure_markers[0].function_name, std::string("identity"));
    CPPL_CHECK_EQ(result.syntax.pure_markers[0].function_location.line, 1u);
}

CPPL_TEST(a_law_without_a_proposition_is_rejected) {
    Recognized result;
    recognize("law incomplete(int x)\n    expects (x > 0);\n", result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.laws.empty());
}

CPPL_TEST(a_law_with_two_propositions_is_rejected) {
    Recognized result;
    recognize("law twice(int x)\n    proves (x == x)\n    ensures (x == x);\n", result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.laws.empty());
}

CPPL_TEST(a_trusted_law_is_recognized_as_an_explicit_assumption) {
    Recognized result;
    recognize("trusted law assumed(int x)\n    proves (x == x);\n", result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.laws.size() == 1);
    CPPL_CHECK(result.syntax.laws[0].trusted);
    CPPL_CHECK(result.syntax.laws[0].name == "assumed");
}

CPPL_TEST(an_ordinary_law_is_not_trusted) {
    Recognized result;
    recognize("law ordinary(int x)\n    proves (x == x);\n", result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.laws.size() == 1);
    CPPL_CHECK(!result.syntax.laws[0].trusted);
}

// The span the projector blanks must cover `trusted` as well, or the keyword
// reaches Clang as ordinary C++ and the declaration fails to parse.
CPPL_TEST(a_trusted_law_span_covers_its_keyword) {
    Recognized result;
    const std::string text = "trusted law assumed(int x)\n    proves (x == x);\n";
    recognize(text, result);

    CPPL_CHECK(result.syntax.laws.size() == 1);
    CPPL_CHECK(result.syntax.laws[0].range.span.offset == 0);
    CPPL_CHECK(text.substr(result.syntax.laws[0].range.span.offset, 7) == "trusted");
}

CPPL_TEST(a_proof_declaration_is_recognized) {
    Recognized result;
    recognize("proof holds(int x)\n    proves (identity_law(x))\n{\n    refl;\n}\n", result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.proofs.size(), std::size_t{1});
    CPPL_CHECK_EQ(result.syntax.proofs[0].name, std::string("holds"));
    CPPL_CHECK_EQ(result.syntax.proofs[0].statements.size(), std::size_t{1});
    CPPL_CHECK(result.syntax.proofs[0].statements[0].kind == cppl::frontend::ProofStatementKind::Reflexivity);
}

CPPL_TEST(a_proof_statement_naming_another_proof_is_recognized) {
    Recognized result;
    recognize("proof a(int x)\n    proves (first(x))\n{\n    refl;\n}\n"
              "proof b(int x)\n    proves (second(x))\n{\n    exact a;\n}\n"
              "proof c(int x)\n    proves (third(x))\n{\n    apply a;\n}\n",
              result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.proofs.size(), std::size_t{3});
    CPPL_CHECK(result.syntax.proofs[1].statements[0].kind == cppl::frontend::ProofStatementKind::Exact);
    CPPL_CHECK_EQ(result.syntax.proofs[1].statements[0].reference, std::string("a"));
    CPPL_CHECK(result.syntax.proofs[2].statements[0].kind == cppl::frontend::ProofStatementKind::Apply);
}

CPPL_TEST(the_terms_a_proof_reference_is_instantiated_at_are_delimited) {
    Recognized result;
    recognize("proof b(unsigned x)\n    proves (second(x))\n{\n    exact a(41u, add(x, 1u));\n}\n", result);

    CPPL_CHECK(!result.engine.has_errors());
    const cppl::frontend::ProofStatement& statement = result.syntax.proofs[0].statements[0];
    CPPL_CHECK(statement.kind == cppl::frontend::ProofStatementKind::Exact);
    CPPL_CHECK_EQ(statement.reference, std::string("a"));
    CPPL_CHECK_EQ(statement.arguments.size(), std::size_t{2});

    // The comma inside the nested call does not separate arguments.
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(
        "proof b(unsigned x)\n    proves (second(x))\n{\n    exact a(41u, add(x, 1u));\n}\n", "main.cpp");
    CPPL_CHECK_EQ(std::string(stream.spelling(statement.arguments[0].span)), std::string("41u"));
    CPPL_CHECK_EQ(std::string(stream.spelling(statement.arguments[1].span)), std::string("add(x, 1u)"));
    CPPL_CHECK_EQ(statement.arguments[0].location.column, std::uint32_t{13});
}

CPPL_TEST(an_empty_instantiation_argument_list_names_no_terms) {
    Recognized result;
    recognize("proof b(int x)\n    proves (second(x))\n{\n    apply a();\n}\n", result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.proofs[0].statements[0].arguments.empty());
}

CPPL_TEST(a_missing_instantiation_argument_is_refused) {
    Recognized result;
    recognize("proof b(int x)\n    proves (second(x))\n{\n    exact a(1, , 2);\n}\n", result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.proofs.empty());
}

CPPL_TEST(an_unbalanced_instantiation_argument_list_is_refused_not_read_as_none) {
    Recognized result;
    recognize("proof b(int x)\n    proves (second(x))\n{\n    exact a(1]);\n}\n", result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.proofs.empty());
}

CPPL_TEST(a_function_returning_a_type_named_proof_is_not_a_proof) {
    Recognized result;
    recognize("proof make(int x);\nproof* holder(int x) { return nullptr; }\n", result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.proofs.empty());
}

CPPL_TEST(an_unsupported_proof_statement_is_refused_rather_than_ignored) {
    Recognized result;
    recognize("proof holds(int x)\n    proves (identity_law(x))\n{\n    let y = x;\n}\n", result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.proofs.empty());
}

CPPL_TEST(an_assumed_premise_is_recognized_with_the_proposition_it_names) {
    Recognized result;
    recognize("proof holds(int x)\n    proves (conditional_law(x))\n"
              "{\n    assume h : identity(x) == x;\n    exact h;\n}\n",
              result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.proofs.size(), std::size_t{1});
    CPPL_CHECK_EQ(result.syntax.proofs[0].statements.size(), std::size_t{2});

    const cppl::frontend::ProofStatement& assumed = result.syntax.proofs[0].statements[0];
    CPPL_CHECK(assumed.kind == cppl::frontend::ProofStatementKind::Assume);
    CPPL_CHECK_EQ(assumed.reference, std::string("h"));

    const cppl::frontend::TokenStream stream =
        cppl::frontend::lex("proof holds(int x)\n    proves (conditional_law(x))\n"
                            "{\n    assume h : identity(x) == x;\n    exact h;\n}\n",
                            "main.cpp");
    CPPL_CHECK_EQ(std::string(stream.spelling(assumed.proposition)), std::string("identity(x) == x"));
    CPPL_CHECK(result.syntax.proofs[0].statements[1].kind == cppl::frontend::ProofStatementKind::Exact);
}

CPPL_TEST(an_assume_without_a_proposition_is_refused) {
    Recognized result;
    recognize("proof holds(int x)\n    proves (l(x))\n{\n    assume h : ;\n}\n", result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.proofs.empty());
}

CPPL_TEST(a_rewrite_statement_names_the_equality_it_transforms_the_goal_with) {
    Recognized result;
    recognize("proof holds(unsigned x)\n    proves (l(x))\n"
              "{\n    assume h : x == 0u;\n    rewrite h;\n"
              "    rewrite other_holds(x);\n    refl;\n}\n",
              result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.proofs[0].statements.size(), std::size_t{4});

    const cppl::frontend::ProofStatement& named = result.syntax.proofs[0].statements[1];
    CPPL_CHECK(named.kind == cppl::frontend::ProofStatementKind::Rewrite);
    CPPL_CHECK_EQ(named.reference, std::string("h"));
    CPPL_CHECK(named.arguments.empty());

    // A rewrite may instantiate the evidence it names, like `exact` and
    // `apply`.
    const cppl::frontend::ProofStatement& instantiated = result.syntax.proofs[0].statements[2];
    CPPL_CHECK(instantiated.kind == cppl::frontend::ProofStatementKind::Rewrite);
    CPPL_CHECK_EQ(instantiated.reference, std::string("other_holds"));
    CPPL_CHECK_EQ(instantiated.arguments.size(), std::size_t{1});
}

CPPL_TEST(a_proof_body_may_carry_more_than_one_statement) {
    Recognized result;
    recognize("proof holds(unsigned x)\n    proves (l(x))\n"
              "{\n    apply conditional(x);\n    refl;\n}\n",
              result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.proofs[0].statements.size(), std::size_t{2});
    CPPL_CHECK(result.syntax.proofs[0].statements[0].kind == cppl::frontend::ProofStatementKind::Apply);
    CPPL_CHECK(result.syntax.proofs[0].statements[1].kind == cppl::frontend::ProofStatementKind::Reflexivity);
}

CPPL_TEST(an_empty_proof_body_is_refused_rather_than_treated_as_evidence) {
    Recognized result;
    recognize("proof holds(int x)\n    proves (identity_law(x))\n{\n}\n", result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.proofs.empty());
}

CPPL_TEST(a_proof_inside_a_class_is_refused_rather_than_half_handled) {
    Recognized result;
    recognize("struct S {\n    proof holds(int x)\n        proves (l(x))\n    {\n        refl;\n"
              "    }\n};\n",
              result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.proofs.empty());
}

CPPL_TEST(a_verified_function_retains_its_contract) {
    Recognized result;
    recognize("verified int identity(int x)\n    ensures (result == x)\n{\n    return x;\n}\n", result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.verified_functions.size(), std::size_t{1});
    const auto& function = result.syntax.verified_functions.front();
    CPPL_CHECK_EQ(function.function_name, std::string("identity"));
    CPPL_CHECK(function.postcondition() != nullptr);
    CPPL_CHECK(function.preconditions().empty());
    CPPL_CHECK(result.syntax.pure_markers.empty());
}

CPPL_TEST(a_contract_clause_on_a_pure_function_is_refused_rather_than_erased) {
    Recognized result;
    recognize("pure int square(int x)\n    ensures (result >= 0)\n{\n    return x * x;\n}\n", result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.pure_markers.empty());
}

// SPEC: WORD-008, WORD-007
// Each name after a comma is another declarator, so `expects(11)` is a variable
// initialized with 11, never a clause of the declarator before it. Read as a
// clause, the formatter would rewrite it and the unit would be treated as C++L.
CPPL_TEST(a_declarator_list_spelled_like_clauses_stays_ordinary) {
    const std::string text = "int type(9), where(10), expects(11), ensures(12), decreases(13);\n"
                             "static int first(1), expects_too(2), ensures(3);\n"
                             "int f(int), expects(4);\n";
    for (const auto mode : {cppl::frontend::RecognitionMode::Compile, cppl::frontend::RecognitionMode::Edit}) {
        cppl::diagnostics::Engine engine;
        const cppl::frontend::TokenStream stream = cppl::frontend::lex(text, "main.cpp");
        const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine, mode);
        CPPL_CHECK(!engine.has_errors());
        CPPL_CHECK(syntax.empty());
    }
}

// The commas of an attribute list and of a trailing return type's template
// arguments do not end the declarator, so a clause after them is still found.
CPPL_TEST(a_clause_after_bracketed_commas_is_still_read_as_a_clause) {
    Recognized attributed;
    recognize("pure int f(int x) [[gnu::hot, gnu::cold]] ensures (result == x) { return x; }\n", attributed);
    CPPL_CHECK(attributed.engine.has_errors());
    CPPL_CHECK(attributed.syntax.pure_markers.empty());

    Recognized trailing;
    recognize("verified auto g(unsigned x) -> std::pair<unsigned, std::pair<unsigned, unsigned>>\n"
              "    ensures (result.first == x)\n{\n    return {x, {x, x}};\n}\n",
              trailing);
    CPPL_CHECK(!trailing.engine.has_errors());
    CPPL_CHECK_EQ(trailing.syntax.verified_functions.size(), std::size_t{1});
    CPPL_CHECK_EQ(trailing.syntax.verified_functions[0].clauses.size(), std::size_t{1});
}

CPPL_TEST(a_law_inside_a_class_is_refused_rather_than_half_handled) {
    Recognized result;
    recognize("struct Account {\n    law nonnegative(int b)\n        proves (b == b);\n};\n", result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.laws.empty());
}

CPPL_TEST(a_law_inside_a_namespace_is_recognized) {
    Recognized result;
    recognize("namespace payments {\nlaw closes(int x)\n    proves (x == x);\n}\n", result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.laws.size(), std::size_t{1});
}

CPPL_TEST(loop_invariants_are_recognized_inside_a_verified_body) {
    Recognized result;
    recognize("verified unsigned f(unsigned n) ensures (result == n) { unsigned i = 0u;\n"
              "  for (; i < n; ++i) invariant (i <= n) { } while (i < n) invariant ((i <= n) && (n >= i)) { ++i; }\n"
              "  return i; }\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.loops.size(), std::size_t{2});
    CPPL_CHECK_EQ(result.syntax.loops[1].invariants.size(), std::size_t{1});
    CPPL_CHECK_EQ(result.syntax.loops[0].function_index, std::size_t{0});
}

CPPL_TEST(a_call_named_invariant_in_a_loop_body_stays_ordinary) {
    Recognized result;
    recognize("void invariant (int); void f(int n) { while (n > 0) invariant (n); for (;;) invariant (1); }\n", result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.empty());
}

CPPL_TEST(a_declaration_of_a_type_named_invariant_as_a_loop_body_stays_ordinary) {
    Recognized result;
    recognize("struct invariant {}; void f(int n) { while (n > 0) invariant (x){}; }\n", result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.empty());
}

CPPL_TEST(a_loop_invariant_outside_a_verified_function_is_refused) {
    Recognized result;
    recognize("unsigned f(unsigned n) { unsigned i = 0u; while (i < n) invariant (i <= n) { ++i; } return i; }\n",
              result);
    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.loops.empty());
}

CPPL_TEST(a_verified_declaration_without_a_body_owns_no_later_body) {
    // The declaration carries a contract and no body. The function after it is
    // not verified, so its loop invariant is outside every verified body, and
    // must be refused rather than attributed to the declaration before it.
    Recognized result;
    recognize("verified unsigned f(unsigned n) ensures (result == n);\n"
              "unsigned g(unsigned n) { unsigned i = 0u; while (i < n) invariant (i <= n) { ++i; } return i; }\n",
              result);
    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.loops.empty());
    CPPL_CHECK_EQ(result.syntax.verified_functions.size(), std::size_t{1});
}

// SPEC: VERIFIED-045, WORD-011
CPPL_TEST(a_contradiction_statement_in_a_verified_body_is_a_claim) {
    Recognized result;
    recognize("proof pinned(unsigned v) proves (v == v) { refl; }\n"
              "verified unsigned f(unsigned x) ensures (result == x) {\n"
              "    if (x > x) contradiction pinned(x);\n"
              "    return x;\n"
              "}\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.engine.diagnostics().empty());
    CPPL_CHECK_EQ(result.syntax.path_contradictions.size(), std::size_t{1});
    const auto& claim = result.syntax.path_contradictions[0];
    CPPL_CHECK_EQ(claim.function_index, std::size_t{0});
    CPPL_CHECK(claim.statement.kind == cppl::frontend::ProofStatementKind::Contradiction);
    CPPL_CHECK_EQ(claim.statement.reference, "pinned");
    CPPL_CHECK_EQ(claim.statement.arguments.size(), std::size_t{1});
    // What is erased stops short of the `;`, which the program keeps.
    CPPL_CHECK_EQ(claim.span.offset, claim.erased.offset);
    CPPL_CHECK_EQ(claim.span.length, claim.erased.length + 1);
    CPPL_CHECK_EQ(claim.statement.location.line, 3u);
    CPPL_CHECK_EQ(claim.statement.location.column, 16u);
}

// SPEC: WORD-002, WORD-011
CPPL_TEST(contradiction_named_anywhere_else_keeps_the_statement_ordinary_cpp) {
    // `contradiction v(x);` declares `v` wherever `contradiction` names a type.
    Recognized result;
    recognize("using contradiction = unsigned;\n"
              "verified unsigned f(unsigned x) ensures (result == x) { contradiction v(x); return v; }\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.path_contradictions.empty());
    CPPL_CHECK_EQ(result.engine.diagnostics().size(), std::size_t{1});
    CPPL_CHECK(result.engine.diagnostics()[0].severity == cppl::diagnostics::Severity::Warning);
}

// SPEC: WORD-002, WORD-011
CPPL_TEST(contradiction_inside_a_proof_leaves_the_word_to_cppl) {
    // The word's uses inside proofs are C++L's own, so they do not make a claim
    // in a verified body ordinary C++.
    Recognized result;
    recognize("law l(unsigned x) expects (x > x) proves (x == 0u) {\n"
              "    assume impossible : x > x;\n"
              "    contradiction impossible;\n"
              "}\n"
              "proof pinned(unsigned v) proves (v == v) { refl; }\n"
              "verified unsigned f(unsigned x) ensures (result == x) { if (x > x) { contradiction pinned(x); } "
              "return x; }\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.engine.diagnostics().empty());
    CPPL_CHECK_EQ(result.syntax.path_contradictions.size(), std::size_t{1});
}

// SPEC: VERIFIED-045, WORD-011
CPPL_TEST(a_claim_outside_a_verified_body_is_refused) {
    Recognized result;
    recognize("unsigned f(unsigned x) { if (x > x) { contradiction pinned; } return x; }\n", result);
    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.path_contradictions.empty());
}

// SPEC: WORD-002, WORD-011
CPPL_TEST(contradiction_where_no_statement_begins_is_not_a_claim) {
    // Inside a `for` header, and in a shape the statement never has: neither is
    // a claim, and each is left to C++ to accept or refuse.
    Recognized result;
    recognize("verified unsigned f(unsigned x) ensures (result == x) {\n"
              "    for (unsigned i = 0u; contradiction h; ++i) {}\n"
              "    contradiction = 3;\n"
              "    return x;\n"
              "}\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.path_contradictions.empty());
}

CPPL_TEST(every_proof_statement_records_where_its_keyword_was_written) {
    // The keyword alone, not the statement: it is what an editor colors.
    const std::string text = "proof p(E s, int a) proves (a == a) {\n"
                             "    assume h : a == a;\n"
                             "    rewrite h;\n"
                             "    apply h;\n"
                             "    exact h;\n"
                             "    contradiction h;\n"
                             "    induction a;\n"
                             "    cases s { omit E::b by contradiction h; E::a => { refl; } }\n"
                             "    decompose s { unnamed(v) => { refl; } }\n"
                             "}\n"
                             "verified int f(int x) ensures (result == x) { if (x != x) { contradiction p; } return x; }\n";
    Recognized result;
    recognize(text, result);
    CPPL_CHECK(!result.engine.has_errors());
    const auto spelled = [&text](const cppl::source::ByteSpan& span) {
        return text.substr(span.offset, span.length);
    };

    const auto& statements = result.syntax.proofs[0].statements;
    const std::vector<std::string> keywords = {"assume",    "rewrite", "apply", "exact",
                                               "contradiction", "induction", "cases", "decompose"};
    CPPL_CHECK_EQ(statements.size(), keywords.size());
    for (std::size_t index = 0; index < keywords.size(); ++index) {
        CPPL_CHECK_EQ(spelled(statements[index].keyword), keywords[index]);
    }
    CPPL_CHECK_EQ(spelled(statements[6].arms[0].statements[0].keyword), std::string("contradiction"));
    CPPL_CHECK_EQ(spelled(statements[6].arms[1].statements[0].keyword), std::string("refl"));
    CPPL_CHECK_EQ(spelled(statements[7].arms[0].statements[0].keyword), std::string("refl"));

    CPPL_CHECK_EQ(result.syntax.path_contradictions.size(), std::size_t{1});
    const auto& claim = result.syntax.path_contradictions[0];
    CPPL_CHECK_EQ(spelled(claim.statement.keyword), std::string("contradiction"));
    CPPL_CHECK_EQ(claim.statement.keyword.offset, claim.span.offset);
}

CPPL_TEST(a_loop_termination_measure_is_recognized_as_a_clause) {
    Recognized result;
    recognize("verified unsigned f(unsigned n) ensures (result == n) { unsigned i = 0u;\n"
              "  while (i < n) invariant (i <= n) decreases (n - i) { ++i; } return i; }\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.loops.size(), std::size_t{1});
    CPPL_CHECK_EQ(result.syntax.loops[0].invariants.size(), std::size_t{1});
    CPPL_CHECK(result.syntax.loops[0].decreases.has_value());
}

CPPL_TEST(a_lexicographic_measure_list_is_refused_rather_than_read_as_its_first_part) {
    // SPEC.md 22.3 states a lexicographic measure as one clause of several
    // parts. Only a single measure is verified here, so the list is refused
    // rather than silently reduced to `n - i`.
    Recognized result;
    recognize("verified unsigned f(unsigned n) ensures (result == n) { unsigned i = 0u;\n"
              "  while (i < n) invariant (i <= n) decreases (n - i, n) { ++i; } return i; }\n",
              result);
    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.loops.empty());
}

CPPL_TEST(a_measure_with_a_comma_inside_a_call_is_one_measure) {
    // The comma belongs to the call's arguments, not to a measure list.
    Recognized result;
    recognize("unsigned pick(unsigned, unsigned);\n"
              "verified unsigned f(unsigned n) ensures (result == n) { unsigned i = 0u;\n"
              "  while (i < n) invariant (i <= n) decreases (pick(n, i)) { ++i; } return i; }\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.loops.size(), std::size_t{1});
    CPPL_CHECK(result.syntax.loops[0].decreases.has_value());
}

CPPL_TEST(a_second_decreases_clause_on_one_loop_is_refused) {
    Recognized result;
    recognize("verified unsigned f(unsigned n) ensures (result == n) { unsigned i = 0u;\n"
              "  while (i < n) invariant (i <= n) decreases (n - i) decreases (n) { ++i; } return i; }\n",
              result);
    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.loops.empty());
}

CPPL_TEST(case_arms_are_nested_proof_statements_with_source_locations) {
    Recognized result;
    recognize("proof p(E s) proves (true) {\n"
              " cases s { E::a => { cases s { unnamed(v) => { refl; } } }\n"
              " unnamed(value) => { assume h : value != 0; refl; } } }",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    const auto& statement = result.syntax.proofs[0].statements[0];
    CPPL_CHECK(statement.kind == cppl::frontend::ProofStatementKind::Cases);
    CPPL_CHECK_EQ(statement.arms.size(), std::size_t{2});
    CPPL_CHECK_EQ(statement.arms[0].location.line, 2u);
    CPPL_CHECK_EQ(statement.arms[1].binders[0], std::string("value"));
    // The parser classifies the label without knowing the subject: `unnamed` is
    // a name a representation reserves, `E::a` is an expression to resolve.
    CPPL_CHECK(statement.arms[1].keyword_label);
    CPPL_CHECK_EQ(statement.arms[1].spelling, std::string("unnamed"));
    CPPL_CHECK(!statement.arms[0].keyword_label);
    CPPL_CHECK_EQ(statement.arms[0].spelling, std::string("E::a"));
}

CPPL_TEST(cases_and_residual_names_remain_ordinary_cpp_identifiers) {
    Recognized result;
    recognize("int cases(int unnamed) { return unnamed; }\n"
              "struct induction { int valueless; };",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.empty());
}

CPPL_TEST(case_nesting_is_bounded_before_recursive_projection) {
    std::string text = "proof p(E s) proves (true) {";
    for (unsigned i = 0; i < 34; ++i)
        text += " cases s { E::a => {";
    text += "refl;";
    for (unsigned i = 0; i < 34; ++i)
        text += "} }";
    text += "}";
    Recognized result;
    recognize(text, result);
    CPPL_CHECK(result.engine.has_errors());
}

// The limits docs/STATUS.md states, pinned at the boundary on both sides so
// that moving one is a deliberate change rather than a silent one.
CPPL_TEST(arms_nest_at_most_thirty_two_deep) {
    const auto nested = [](unsigned depth) {
        std::string text = "proof p(E s) proves (true) {";
        for (unsigned i = 0; i < depth; ++i) {
            text += " cases s { E::a => {";
        }
        text += " refl;";
        for (unsigned i = 0; i < depth; ++i) {
            text += " } }";
        }
        return text + " }";
    };
    Recognized deepest;
    recognize(nested(32), deepest);
    CPPL_CHECK(!deepest.engine.has_errors());

    Recognized deeper;
    recognize(nested(33), deeper);
    CPPL_CHECK(deeper.engine.has_errors());
    CPPL_CHECK(!deeper.engine.diagnostics().empty());
    CPPL_CHECK(deeper.engine.diagnostics()[0].message == "proof arms nest deeper than the supported limit");
}

CPPL_TEST(a_cases_statement_reads_at_most_sixty_four_arms_counting_omissions) {
    const auto statement = [](unsigned arms, unsigned omissions) {
        std::string text = "proof p(E s) proves (true) { cases s {";
        for (unsigned i = 0; i < arms; ++i) {
            text += " E::a" + std::to_string(i) + " => { refl; }";
        }
        for (unsigned i = 0; i < omissions; ++i) {
            text += " omit E::b" + std::to_string(i) + " by contradiction e;";
        }
        return text + " } }";
    };
    const auto arm_count_refused = [](const std::string& text) {
        Recognized result;
        recognize(text, result);
        return !result.engine.diagnostics().empty() &&
               result.engine.diagnostics()[0].message.starts_with("cases requires 1 to 64 arms");
    };
    const auto accepted = [](const std::string& text) {
        Recognized result;
        recognize(text, result);
        return !result.engine.has_errors() && result.syntax.proofs.size() == 1;
    };
    CPPL_CHECK(accepted(statement(64, 0)));
    CPPL_CHECK(arm_count_refused(statement(65, 0)));
    // An omission accounts for a case in place of an arm, so it takes an arm's
    // place in the count too.
    CPPL_CHECK(accepted(statement(63, 1)));
    CPPL_CHECK(arm_count_refused(statement(64, 1)));
}
