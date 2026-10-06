// Contextual recognition: a C++L word is only C++L syntax where ordinary C++
// cannot mean it (SPEC.md 3.1, GRAMMAR.md 1).

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "cppl/testing/test.hpp"
#include "recognizer_test_support.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

using namespace recognizer_test_detail;

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

// SPEC: WORD-008, WORD-014
// A word followed by `::` is the first component of a nested-name-specifier.
// Read as a specifier, `pure::inner g()` would become a pure `::inner g()`: the
// same tokens with another return type, and no diagnostic.
CPPL_TEST(a_cppl_word_followed_by_a_scope_operator_is_a_qualified_name) {
    for (const char* text : {
             "pure::inner g() { return {}; }\n",
             "pure ::inner g() { return {}; }\n",
             "static pure::inner g() { return {}; }\n",
             "inline pure::inner g() { return {}; }\n",
             "constexpr pure::inner g() { return {}; }\n",
             "template <class T> pure::inner g(T) { return {}; }\n",
             "struct S { static pure::inner g() { return {}; } pure::inner h() const { return {}; } };\n",
             "pure::inner* g();\n",
             "pure::inner value;\n",
             "verified::inner g() { return {}; }\n",
             "verified :: inner g() { return {}; }\n",
             "static verified::R g(unsigned x) { return x; }\n",
             "unsafe::inner g() { return {}; }\n",
             "ghost::inner value;\n",
             "void f() { pure::f(); int a = pure::f(); static pure::inner kept; (void)a; }\n",
             "void f() { ghost::inner g; ghost::inner h = g; unsafe::inner u; verified::inner v; }\n",
             "void f() { cases::inner c{}; decompose::inner d{}; contradiction::inner e; }\n",
             "verified Factory::make() { return {}; }\n",
             "pure Factory::make() { return {}; }\n",
             "verified ns::value;\n",
             "verified unsigned f(unsigned x) ensures (result == x) { cases::inner c{}; ghost::inner g; return x; }\n",
         }) {
        Recognized result;
        recognize(text, result);
        CPPL_CHECK(result.engine.diagnostics().empty());
        CPPL_CHECK(result.syntax.pure_markers.empty());
        CPPL_CHECK(result.syntax.unsafe_functions.empty());
        CPPL_CHECK(result.syntax.ghost_declarations.empty());
        CPPL_CHECK(result.syntax.path_splits.empty());
        CPPL_CHECK(result.syntax.verified_functions.size() <= 1);
    }
}

// SPEC: WORD-014
// After the specifiers, a qualified return type may begin with a word: here
// `pure::inner` is the type, and only `verified` is a specifier.
CPPL_TEST(a_return_type_qualified_by_a_cppl_word_follows_the_specifiers) {
    Recognized result;
    const std::string text = "verified pure::inner f(int x) ensures (result.v == x) { return {x}; }\n"
                             "verified pure unsigned g(unsigned x) ensures (result == x) { return x; }\n";
    recognize(text, result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.verified_functions.size(), std::size_t{2});
    const auto& qualified = result.syntax.verified_functions[0];
    CPPL_CHECK_EQ(qualified.function_name, std::string("f"));
    CPPL_CHECK_EQ(text.substr(qualified.return_type.offset, qualified.return_type.length), std::string("pure::inner "));
    CPPL_CHECK_EQ(result.syntax.pure_markers.size(), std::size_t{1});
    CPPL_CHECK_EQ(result.syntax.pure_markers[0].function_name, std::string("g"));
}

// SPEC: WORD-008, WORD-015
// A word naming a type may be followed by the decl-specifiers C++ admits after
// a type name, an operator-function-id, a template-id declarator or an
// operator's alternative spelling. None of those states a return type after the
// word, so none is a C++L declaration.
CPPL_TEST(a_cppl_word_before_specifiers_or_an_operator_is_the_type_it_names) {
    for (const char* text : {
             "verified const c{};\n",
             "pure const c{};\n",
             "verified volatile c{};\n",
             "verified static s;\n",
             "verified constexpr c{};\n",
             "verified typedef alias;\n",
             "verified extern e;\n",
             "verified thread_local t;\n",
             "pure static make();\n",
             "verified inline make() { return {}; }\n",
             "verified constexpr make() { return {}; }\n",
             "unsafe const u{};\n",
             "struct S { verified mutable m; verified virtual get() const; verified static shared; };\n",
             "template <> verified pick<int>(int) { return {}; }\n",
             "verified operator*(verified a, verified b) { return a; }\n",
             "verified operator&(verified a, verified b) { return a; }\n",
             "verified S::operator*(verified b) { return b; }\n",
             "void f() { verified const local{}; verified static kept; pure and_eq mask; unsafe or_eq mask; }\n",
         }) {
        Recognized result;
        recognize(text, result);
        CPPL_CHECK(result.engine.diagnostics().empty());
        CPPL_CHECK(result.syntax.empty());
    }
}

// SPEC: CLASS-008
// An operator function's parameter list opens after its operator: the `()` of
// `operator()` and the `[]` of `operator[]` are its name, never its parameter
// list, so the declarations its contract is read from take its parameters. An
// operator function no C++L marks stays ordinary C++.
CPPL_TEST(an_operator_function_is_verified_over_its_own_parameters) {
    const std::string text = "struct A {\n"
                             "    verified unsigned operator()(unsigned k) const ensures (result == k) { return k; }\n"
                             "    verified unsigned operator[](unsigned i) const ensures (result == i) { return i; }\n"
                             "};\n"
                             "verified unsigned operator+(A a, unsigned b) ensures (result == b) { return b; }\n";
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(text, "main.cpp");
    cppl::diagnostics::Engine engine;
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(syntax.verified_functions.size(), std::size_t{3});
    const std::vector<std::pair<std::string, std::string>> expected = {
        {"operator()", "unsigned k"}, {"operator[]", "unsigned i"}, {"operator+", "A a, unsigned b"}};
    for (std::size_t index = 0; index < expected.size() && index < syntax.verified_functions.size(); ++index) {
        const cppl::frontend::VerifiedFunction& verified = syntax.verified_functions[index];
        CPPL_CHECK_EQ(verified.function_name, expected[index].first);
        CPPL_CHECK_EQ(std::string(stream.spelling(verified.parameters)), expected[index].second);
    }

    Recognized ordinary;
    recognize("struct B {\n"
              "    bool operator==(const B&) const = default;\n"
              "    unsigned operator()(unsigned k) const { return k; }\n"
              "    unsigned operator[](unsigned i) const { return i; }\n"
              "};\n",
              ordinary);
    CPPL_CHECK(ordinary.engine.diagnostics().empty());
    CPPL_CHECK(ordinary.syntax.empty());
}

// SPEC: WORD-015
// After those specifiers, a return type still makes the word a specifier: what
// follows the specifiers decides, as it does without them.
CPPL_TEST(a_return_type_after_ordinary_specifiers_still_follows_a_cppl_specifier) {
    Recognized result;
    recognize("verified inline unsigned f(unsigned x) ensures (result == x) { return x; }\n"
              "verified const unsigned g(unsigned x) ensures (result == x) { return x; }\n"
              "verified static unsigned h(unsigned x) ensures (result == x) { return x; }\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.verified_functions.size(), std::size_t{3});
}

// SPEC: CONTRACT-011, WORD-015
// `explicit` stands only on what has no return type, so the word before it is
// never one: a verified constructor or conversion function is refused, after any
// ordinary specifier, rather than handed to Clang as an unknown type.
CPPL_TEST(a_verified_constructor_or_conversion_after_ordinary_specifiers_is_refused) {
    for (const char* text : {"struct Meter { verified constexpr Meter(unsigned v) : value(v) {} unsigned value; };\n",
                             "struct Meter { verified explicit Meter(unsigned v) : value(v) {} unsigned value; };\n",
                             "struct Flag { verified explicit operator bool() const { return true; } };\n"}) {
        Recognized result;
        recognize(text, result);
        CPPL_CHECK(result.engine.has_errors());
        CPPL_CHECK(result.syntax.verified_functions.empty());
    }
}

// SPEC: WORD-008, WORD-017
// A constructor's mem-initializers follow `:` and belong to its body, so a
// member named `expects` initialized there is no clause, whatever specifier
// leads the constructor. A trailing return type names its type first, so a
// clause word that begins it, after cv-qualifiers or after `::`, is that type.
CPPL_TEST(a_mem_initializer_or_a_trailing_return_type_named_like_a_clause_is_no_clause) {
    for (const char* text : {
             "struct S { int expects; explicit S(int a) : expects(a) {} };\n",
             "struct S { int ensures; constexpr S() : ensures(1) {} };\n",
             "struct S { int decreases; inline S(int a) : decreases(a) {} };\n",
             "struct S { int ensures; explicit constexpr S(int v) noexcept : ensures(v) {} };\n",
             "struct S { int a, expects; explicit S(int v) : a(v), expects(v) {} };\n",
             "auto pick() -> ensures (&)[3] { return table; }\n",
             "inline auto maker() -> ensures (*)(int) { return make; }\n",
             "static auto none() -> expects (*)() { return nullptr; }\n",
             "auto constant() -> const ensures (&)[3] { return table; }\n",
             "auto qualified() -> ns::decreases (&)[2] { return measures; }\n",
             "struct H { auto member() const -> ensures (&)[3] { return table; } };\n",
         }) {
        Recognized result;
        recognize(text, result);
        CPPL_CHECK(result.engine.diagnostics().empty());
        CPPL_CHECK(result.syntax.empty());
    }
}

// SPEC: WORD-017
// A clause after a trailing return type that has named its type, or before a
// constructor's `:`, is still a clause.
CPPL_TEST(a_clause_after_a_named_trailing_return_type_or_before_mem_initializers_is_read) {
    Recognized result;
    recognize("verified auto f(unsigned x) -> unsigned ensures (result == x) { return x; }\n"
              "struct S { int m; explicit S(int a) expects (a > 0) : m(a) {} };\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.verified_functions.size(), std::size_t{1});
    CPPL_CHECK_EQ(result.syntax.verified_functions[0].clauses.size(), std::size_t{1});
    CPPL_CHECK_EQ(result.syntax.unchecked_clauses.size(), std::size_t{1});
    CPPL_CHECK_EQ(result.syntax.unchecked_clauses[0].function_name, std::string("S"));
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
