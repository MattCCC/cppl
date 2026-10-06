// Recognizer tests (unit_recognizer_test): proof declarations, their
// statements and where each is written, and case arms.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "cppl/testing/test.hpp"
#include "recognizer_test_support.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace recognizer_test_detail;

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

CPPL_TEST(every_proof_statement_records_where_its_keyword_was_written) {
    // The keyword alone, not the statement: it is what an editor colors.
    const std::string text =
        "proof p(E s, int a) proves (a == a) {\n"
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
    const std::vector<std::string> keywords = {"assume",        "rewrite",   "apply", "exact",
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

// A label is an id-expression (GRAMMAR.md 5.9), so any part of it may carry
// template arguments, including a `>>` that closes two lists at once. An
// enumerator of a class template's member enumeration can be named no other way.
CPPL_TEST(case_labels_are_id_expressions_whose_parts_carry_template_arguments) {
    Recognized result;
    recognize("proof p(M s) proves (true) {\n"
              " cases s { Machine<int>::Mode::on => { refl; }\n"
              " ::Outer<Box<int>>::Mode::off => { refl; }\n"
              " omit Machine<long>::Mode::idle by contradiction e;\n"
              " alternative<1>(v) => { refl; } } }",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    const auto& statement = result.syntax.proofs[0].statements[0];
    CPPL_CHECK_EQ(statement.arms.size(), std::size_t{4});
    CPPL_CHECK_EQ(statement.arms[0].spelling, std::string("Machine<int>::Mode::on"));
    CPPL_CHECK_EQ(statement.arms[1].spelling, std::string("::Outer<Box<int>>::Mode::off"));
    CPPL_CHECK(statement.arms[2].omitted);
    CPPL_CHECK_EQ(statement.arms[2].spelling, std::string("Machine<long>::Mode::idle"));
    CPPL_CHECK_EQ(statement.arms[3].spelling, std::string("alternative<1>"));
    CPPL_CHECK_EQ(statement.arms[3].binders[0], std::string("v"));
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
