// Recognizer tests (unit_recognizer_test): contracts, loop clauses and
// termination measures, and refinements.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "cppl/testing/test.hpp"
#include "recognizer_test_support.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

using namespace recognizer_test_detail;

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

// SPEC: WORD-008, WORD-016
// A loop body that declares locals of a type named `decreases` or `invariant`,
// one or several, with braced initializers, in every loop form. One clause
// holding a declarator, then braces holding no statement and a `;` or `,`, is
// a declaration (GRAMMAR.md 25).
CPPL_TEST(a_loop_body_declaring_locals_of_a_clause_named_type_stays_ordinary) {
    for (const char* text : {
             "void f(int n) { while (n-- > 0) decreases (k) {n}; }\n",
             "void f(int n) { for (int i = 0; i < n; ++i) decreases (m) {i}; }\n",
             "void f(int n) { do decreases (p) {7}; while (--n > 0); }\n",
             "void f(int n) { for (int i = 0; i < n; ++i) invariant (y) {i}, (z) {i + 1}; }\n",
             "void f(int n) { while (n < 2) invariant (w) {n++}, (q) {0}; }\n",
             "void f(int n) { do invariant (x) {n}, (y) {n}; while (--n > 0); }\n",
             "void f(int n) { while (n-- > 0) decreases (*p) {nullptr}, (&r) {kept}, (a[2]) {}; }\n",
             "void f(int n) { int k = 0; while (n-- > 0) decreases (k) {n}; for (;;) decreases (m) {k}; }\n",
         }) {
        Recognized result;
        recognize(text, result);
        CPPL_CHECK(result.engine.diagnostics().empty());
        CPPL_CHECK(result.syntax.empty());
    }
}

// SPEC: LOOP-001, WORD-016
// The same words stay clauses wherever the declaration cannot be meant: the
// parentheses hold an expression, or the braces hold a statement, which a
// braced initializer never does, even when a `;` follows the loop.
CPPL_TEST(loop_clauses_before_a_body_that_cannot_be_an_initializer_stay_clauses) {
    Recognized result;
    recognize("verified unsigned f(unsigned n) ensures (result == n) { unsigned i = 0u;\n"
              "  while (i < n) invariant (i <= n) {};\n"
              "  while (i < n) invariant (ok) { ++i; };\n"
              "  while (i < n) decreases (n - i) {};\n"
              "  do invariant (i <= n) { ++i; } while (i < n);\n"
              "  return i; }\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.loops.size(), std::size_t{4});
}

// SPEC: REFINE-001, WORD-016
// A refinement's `where` follows its base type, and a type-id never ends in `,`
// or an operator. After one, the word is the next declarator of a C++
// declaration, or an operand of its initializer.
CPPL_TEST(where_after_a_comma_or_an_operator_is_not_a_refinement_predicate) {
    for (const char* text :
         {"type a = 5, where (6);\n", "void f() { type b = 7, where (8); }\n", "type c = 1 + where (2);\n",
          "type d = x ? where (3) : 0;\n", "type e = b < c, where (9);\n"}) {
        Recognized result;
        recognize(text, result);
        CPPL_CHECK(result.engine.diagnostics().empty());
        CPPL_CHECK(result.syntax.empty());
    }
    Recognized result;
    recognize("type Ordered = std::pair<int, int> where (self.first <= self.second);\n"
              "type Present = int* where (self != nullptr);\n"
              "type Small = unsigned where (self < 10u);\n",
              result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.refinement_types.size(), std::size_t{3});
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

// SPEC: TERMINATION-004
// A lexicographic measure is one clause whose components its top-level commas
// separate, each read whole, never reduced to its first part.
CPPL_TEST(a_lexicographic_measure_list_is_read_component_by_component) {
    Recognized result;
    const std::string text = "verified unsigned f(unsigned n) ensures (result == n) { unsigned i = 0u;\n"
                             "  while (i < n) invariant (i <= n) decreases (n - i, n) { ++i; } return i; }\n";
    recognize(text, result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.loops.size(), std::size_t{1});
    const std::optional<cppl::frontend::Clause>& decreases = result.syntax.loops[0].decreases;
    CPPL_CHECK(decreases.has_value());
    if (!decreases.has_value()) {
        return;
    }
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(text, "main.cpp");
    const auto components = cppl::frontend::measure_components(stream, *decreases);
    CPPL_CHECK_EQ(components.size(), std::size_t{2});
    CPPL_CHECK_EQ(std::string(stream.spelling(components[0].expression)), std::string("n - i"));
    CPPL_CHECK_EQ(std::string(stream.spelling(components[1].expression)), std::string("n"));
    CPPL_CHECK_EQ(components[1].location.column, 54u);
}

CPPL_TEST(a_measure_with_a_comma_inside_a_call_is_one_measure) {
    // The comma belongs to the call's arguments, not to a measure list.
    Recognized result;
    const std::string text = "unsigned pick(unsigned, unsigned);\n"
                             "verified unsigned f(unsigned n) ensures (result == n) { unsigned i = 0u;\n"
                             "  while (i < n) invariant (i <= n) decreases (pick(n, i)) { ++i; } return i; }\n";
    recognize(text, result);
    CPPL_CHECK(!result.engine.has_errors());
    CPPL_CHECK_EQ(result.syntax.loops.size(), std::size_t{1});
    const std::optional<cppl::frontend::Clause>& decreases = result.syntax.loops[0].decreases;
    CPPL_CHECK(decreases.has_value());
    if (!decreases.has_value()) {
        return;
    }
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(text, "main.cpp");
    CPPL_CHECK_EQ(cppl::frontend::measure_components(stream, *decreases).size(), std::size_t{1});
}

// SPEC: TERMINATION-004
// A function may ask that it terminate, with one measure or a list; an empty
// component is refused rather than read as none.
CPPL_TEST(a_function_measure_is_a_clause_and_an_empty_component_is_refused) {
    Recognized accepted;
    recognize("verified unsigned f(unsigned m, unsigned n) ensures (result == 0u) decreases (m, n) { return 0u; }\n",
              accepted);
    CPPL_CHECK(!accepted.engine.has_errors());
    CPPL_CHECK_EQ(accepted.syntax.verified_functions.size(), std::size_t{1});
    CPPL_CHECK(accepted.syntax.verified_functions[0].measure() != nullptr);

    for (const char* text : {"verified unsigned f(unsigned n) ensures (result == 0u) decreases (n, ) { return 0u; }\n",
                             "verified unsigned f(unsigned n) ensures (result == 0u) { unsigned i = n;\n"
                             "  while (i > 0u) decreases (, i) { --i; } return i; }\n"}) {
        Recognized refused;
        recognize(text, refused);
        CPPL_CHECK(refused.engine.has_errors());
    }
}

CPPL_TEST(a_second_decreases_clause_on_one_loop_is_refused) {
    Recognized result;
    recognize("verified unsigned f(unsigned n) ensures (result == n) { unsigned i = 0u;\n"
              "  while (i < n) invariant (i <= n) decreases (n - i) decreases (n) { ++i; } return i; }\n",
              result);
    CPPL_CHECK(result.engine.has_errors());
    CPPL_CHECK(result.syntax.loops.empty());
}
