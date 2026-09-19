// Contextual recognition: a C++L word is only C++L syntax where ordinary C++
// cannot mean it (SPEC.md 3.1, GRAMMAR.md 1).

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/testing/test.hpp"

#include <string>

namespace {

struct Recognized {
    cppl::diagnostics::Engine engine;
    cppl::frontend::Syntax syntax;
};

void recognize(const std::string& text, Recognized& out) {
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(text, "main.cpp");
    out.syntax = cppl::frontend::recognize(stream, out.engine);
}

}  // namespace

CPPL_TEST(a_law_declaration_is_recognized) {
    Recognized result;
    recognize("law identity_returns_input(int x)\n    ensures(identity(x) == x);\n", result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.laws.size(), std::size_t{1});
    CPPL_CHECK_EQ(result.syntax.laws[0].name, std::string("identity_returns_input"));
    CPPL_CHECK_EQ(result.syntax.laws[0].clauses.size(), std::size_t{1});
    CPPL_CHECK(result.syntax.laws[0].proposition() != nullptr);
}

CPPL_TEST(ordinary_identifiers_named_after_cppl_words_stay_ordinary) {
    Recognized result;
    recognize(
        "int law = 1;\n"
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
    recognize("law incomplete(int x)\n    expects(x > 0);\n", result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.laws.empty());
}

CPPL_TEST(a_law_with_two_propositions_is_rejected) {
    Recognized result;
    recognize("law twice(int x)\n    ensures(x == x)\n    ensures(x == x);\n", result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.laws.empty());
}

CPPL_TEST(a_trusted_law_is_refused_rather_than_treated_as_proven) {
    Recognized result;
    recognize("trusted law assumed(int x)\n    ensures(x == x);\n", result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.laws.empty());
}

CPPL_TEST(a_proof_declaration_is_recognized) {
    Recognized result;
    recognize("proof holds(int x)\n    proves(identity_law(x))\n{\n    refl;\n}\n", result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.proofs.size(), std::size_t{1});
    CPPL_CHECK_EQ(result.syntax.proofs[0].name, std::string("holds"));
    CPPL_CHECK_EQ(result.syntax.proofs[0].statements.size(), std::size_t{1});
    CPPL_CHECK(result.syntax.proofs[0].statements[0].kind ==
               cppl::frontend::ProofStatementKind::Reflexivity);
}

CPPL_TEST(a_proof_statement_naming_another_proof_is_recognized) {
    Recognized result;
    recognize(
        "proof a(int x)\n    proves(first(x))\n{\n    refl;\n}\n"
        "proof b(int x)\n    proves(second(x))\n{\n    exact a;\n}\n"
        "proof c(int x)\n    proves(third(x))\n{\n    apply a;\n}\n",
        result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.proofs.size(), std::size_t{3});
    CPPL_CHECK(result.syntax.proofs[1].statements[0].kind ==
               cppl::frontend::ProofStatementKind::Exact);
    CPPL_CHECK_EQ(result.syntax.proofs[1].statements[0].reference, std::string("a"));
    CPPL_CHECK(result.syntax.proofs[2].statements[0].kind ==
               cppl::frontend::ProofStatementKind::Apply);
}

CPPL_TEST(the_terms_a_proof_reference_is_instantiated_at_are_delimited) {
    Recognized result;
    recognize("proof b(unsigned x)\n    proves(second(x))\n{\n    exact a(41u, add(x, 1u));\n}\n",
              result);

    CPPL_CHECK(!result.engine.has_errors());
    const cppl::frontend::ProofStatement& statement = result.syntax.proofs[0].statements[0];
    CPPL_CHECK(statement.kind == cppl::frontend::ProofStatementKind::Exact);
    CPPL_CHECK_EQ(statement.reference, std::string("a"));
    CPPL_CHECK_EQ(statement.arguments.size(), std::size_t{2});

    // The comma inside the nested call does not separate arguments.
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(
        "proof b(unsigned x)\n    proves(second(x))\n{\n    exact a(41u, add(x, 1u));\n}\n",
        "main.cpp");
    CPPL_CHECK_EQ(std::string(stream.spelling(statement.arguments[0].span)),
                  std::string("41u"));
    CPPL_CHECK_EQ(std::string(stream.spelling(statement.arguments[1].span)),
                  std::string("add(x, 1u)"));
    CPPL_CHECK_EQ(statement.arguments[0].location.column, std::uint32_t{13});
}

CPPL_TEST(an_empty_instantiation_argument_list_names_no_terms) {
    Recognized result;
    recognize("proof b(int x)\n    proves(second(x))\n{\n    apply a();\n}\n", result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK(result.syntax.proofs[0].statements[0].arguments.empty());
}

CPPL_TEST(a_missing_instantiation_argument_is_refused) {
    Recognized result;
    recognize("proof b(int x)\n    proves(second(x))\n{\n    exact a(1, , 2);\n}\n", result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.proofs.empty());
}

CPPL_TEST(an_unbalanced_instantiation_argument_list_is_refused_not_read_as_none) {
    Recognized result;
    recognize("proof b(int x)\n    proves(second(x))\n{\n    exact a(1]);\n}\n", result);

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
    recognize("proof holds(int x)\n    proves(identity_law(x))\n{\n    let y = x;\n}\n", result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.proofs.empty());
}

CPPL_TEST(an_assumed_premise_is_recognized_with_the_proposition_it_names) {
    Recognized result;
    recognize("proof holds(int x)\n    proves(conditional_law(x))\n"
              "{\n    assume h : identity(x) == x;\n    exact h;\n}\n",
              result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.proofs.size(), std::size_t{1});
    CPPL_CHECK_EQ(result.syntax.proofs[0].statements.size(), std::size_t{2});

    const cppl::frontend::ProofStatement& assumed = result.syntax.proofs[0].statements[0];
    CPPL_CHECK(assumed.kind == cppl::frontend::ProofStatementKind::Assume);
    CPPL_CHECK_EQ(assumed.reference, std::string("h"));

    const cppl::frontend::TokenStream stream = cppl::frontend::lex(
        "proof holds(int x)\n    proves(conditional_law(x))\n"
        "{\n    assume h : identity(x) == x;\n    exact h;\n}\n",
        "main.cpp");
    CPPL_CHECK_EQ(std::string(stream.spelling(assumed.proposition)),
                  std::string("identity(x) == x"));
    CPPL_CHECK(result.syntax.proofs[0].statements[1].kind ==
               cppl::frontend::ProofStatementKind::Exact);
}

CPPL_TEST(an_assume_without_a_proposition_is_refused) {
    Recognized result;
    recognize("proof holds(int x)\n    proves(l(x))\n{\n    assume h : ;\n}\n", result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.proofs.empty());
}

CPPL_TEST(a_rewrite_statement_names_the_equality_it_transforms_the_goal_with) {
    Recognized result;
    recognize("proof holds(unsigned x)\n    proves(l(x))\n"
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
    recognize("proof holds(unsigned x)\n    proves(l(x))\n"
              "{\n    apply conditional(x);\n    refl;\n}\n",
              result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.proofs[0].statements.size(), std::size_t{2});
    CPPL_CHECK(result.syntax.proofs[0].statements[0].kind ==
               cppl::frontend::ProofStatementKind::Apply);
    CPPL_CHECK(result.syntax.proofs[0].statements[1].kind ==
               cppl::frontend::ProofStatementKind::Reflexivity);
}

CPPL_TEST(an_empty_proof_body_is_refused_rather_than_treated_as_evidence) {
    Recognized result;
    recognize("proof holds(int x)\n    proves(identity_law(x))\n{\n}\n", result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.proofs.empty());
}

CPPL_TEST(a_proof_inside_a_class_is_refused_rather_than_half_handled) {
    Recognized result;
    recognize("struct S {\n    proof holds(int x)\n        proves(l(x))\n    {\n        refl;\n"
              "    }\n};\n",
              result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.proofs.empty());
}

CPPL_TEST(a_verified_function_retains_its_contract) {
    Recognized result;
    recognize("verified int identity(int x)\n    ensures(result == x)\n{\n    return x;\n}\n",
              result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.verified_functions.size(), std::size_t{1});
    const auto& function = result.syntax.verified_functions.front();
    CPPL_CHECK_EQ(function.function_name, std::string("identity"));
    CPPL_CHECK(function.postcondition() != nullptr);
    CPPL_CHECK(function.precondition() == nullptr);
    CPPL_CHECK(result.syntax.pure_markers.empty());
}

CPPL_TEST(a_contract_clause_on_a_pure_function_is_refused_rather_than_erased) {
    Recognized result;
    recognize("pure int square(int x)\n    ensures(result >= 0)\n{\n    return x * x;\n}\n",
              result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.pure_markers.empty());
}

CPPL_TEST(a_law_inside_a_class_is_refused_rather_than_half_handled) {
    Recognized result;
    recognize("struct Account {\n    law nonnegative(int b)\n        ensures(b == b);\n};\n",
              result);

    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.laws.empty());
}

CPPL_TEST(a_law_inside_a_namespace_is_recognized) {
    Recognized result;
    recognize("namespace payments {\nlaw closes(int x)\n    ensures(x == x);\n}\n", result);

    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.laws.size(), std::size_t{1});
}
