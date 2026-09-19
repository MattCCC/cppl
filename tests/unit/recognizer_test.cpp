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

CPPL_TEST(a_proof_declaration_is_refused_rather_than_ignored) {
    Recognized result;
    recognize("proof reflexive(int x)\n    proves(Eq<int>(x, x))\n{\n    refl;\n}\n", result);

    CPPL_CHECK(result.engine.has_errors());
}

CPPL_TEST(a_verified_function_is_refused_rather_than_ignored) {
    Recognized result;
    recognize("verified int identity(int x)\n    ensures(result == x)\n{\n    return x;\n}\n",
              result);

    CPPL_CHECK(result.engine.has_errors());
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
