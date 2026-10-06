// Formatter tests (formatter_test): the clauses of laws, functions and
// loops, and refinements. formatter_test.cpp lists the canonical
// rules under test.

#include "cppl/formatter/format.hpp"
#include "cppl/testing/test.hpp"
#include "formatter_test_support.hpp"

#include <cstddef>
#include <string>
#include <vector>

using namespace formatter_test_detail;

using namespace cppl;

// -----------------------------------------------------------------------------
// Laws
// -----------------------------------------------------------------------------

CPPL_TEST(law_proves_clause_moves_to_its_own_continuation_line) {
    const std::string input = "law identity(int x) proves (x == x);\n";
    CPPL_CHECK_EQ(format_text(input), "law identity(int x)\n    proves (x == x);\n");
}

CPPL_TEST(law_expects_then_proves_are_each_on_their_own_line_in_grammar_order) {
    const std::string input = "law bounded(unsigned x) expects(x<10u) proves(x+1u<=10u);\n";
    CPPL_CHECK_EQ(format_text(input), "law bounded(unsigned x)\n"
                                      "    expects (x < 10u)\n"
                                      "    proves (x + 1u <= 10u);\n");
}

CPPL_TEST(law_with_body_places_opening_brace_at_law_indentation) {
    const std::string input = "law given_zero(unsigned x) expects(x==0u) proves(x==0u){assume h:x==0u;exact h;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("law given_zero(unsigned x)\n    expects (x == 0u)\n    proves (x == 0u)\n{\n") !=
               std::string::npos);
}

CPPL_TEST(trusted_law_uses_the_same_clause_layout) {
    const std::string input = "trusted law external_fact(int x) expects(x>=0) proves(x>=0);\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("trusted law external_fact(int x)\n    expects (x >= 0)\n    proves (x >= 0);\n") !=
               std::string::npos);
}

CPPL_TEST(law_nested_predicate_parentheses_are_preserved) {
    const std::string input = "law nested(int x,int y) proves(((x+y)==(y+x))&&(x==x));\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("\n    proves (") != std::string::npos);
    CPPL_CHECK(count_occurrences(formatted, "proves") == 1);
}

CPPL_TEST(law_implication_stays_inside_the_proves_predicate) {
    const std::string input = "law implication(unsigned x) proves(x==0u->x+0u==0u);\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("law implication(unsigned x)\n    proves (") != std::string::npos);
    CPPL_CHECK(formatted.find("->") != std::string::npos);
}

CPPL_TEST(law_forall_form_is_not_split_into_fake_clauses) {
    const std::string input = "law all_identity() proves(forall(unsigned x){x==x});\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("\n    proves (") != std::string::npos);
    CPPL_CHECK(count_occurrences(formatted, "proves") == 1);
    CPPL_CHECK(formatted.find("forall") != std::string::npos);
}

CPPL_TEST(law_formal_equality_stays_inside_clause) {
    const std::string input = "law eq(unsigned x) proves(Eq<unsigned>(x,x));\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("\n    proves (") != std::string::npos);
    CPPL_CHECK(formatted.find("Eq<unsigned>") != std::string::npos);
}

CPPL_TEST(class_scope_law_clauses_indent_relative_to_the_law_declaration) {
    const std::string input = "struct Box {\nlaw identity(int x) proves(x==x);\n};\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("    law identity(int x)\n        proves (x == x);\n") != std::string::npos);
}

CPPL_TEST(already_canonical_law_is_a_no_op) {
    const std::string input = "law identity(int x)\n    proves (x == x);\n";
    const formatter::FormatResult result = document_result(input);
    CPPL_CHECK(result.ok);
    CPPL_CHECK(result.edits.empty());
}

CPPL_TEST(comments_in_law_predicates_survive_formatting) {
    const std::string input = "law commented(int x) proves(/* identity */x==x);\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("/* identity */") != std::string::npos);
    CPPL_CHECK(formatted.find("\n    proves (") != std::string::npos);
}

// -----------------------------------------------------------------------------
// Function contracts, modifiers, and ordinary declarators
// -----------------------------------------------------------------------------

CPPL_TEST(function_expects_clause_gets_canonical_spacing_and_line_placement) {
    const std::string input = "verified int f(int x) expects(x>=0){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("verified int f(int x)\n    expects (x >= 0)\n{\n") != std::string::npos);
}

CPPL_TEST(function_ensures_clause_gets_canonical_spacing_and_line_placement) {
    const std::string input = "verified int f(int x) ensures(result==x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("verified int f(int x)\n    ensures (result == x)\n{\n") != std::string::npos);
}

CPPL_TEST(function_expects_then_ensures_stay_in_source_and_grammar_order) {
    const std::string input = "verified int f(int x) expects(x>=0) ensures(result>=0){return x;}\n";
    CPPL_CHECK_EQ(format_text(input), "verified int f(int x)\n"
                                      "    expects (x >= 0)\n"
                                      "    ensures (result >= 0)\n"
                                      "{\n"
                                      "    return x;\n"
                                      "}\n");
}

CPPL_TEST(function_decreases_clause_gets_its_own_continuation_line) {
    const std::string input = "pure unsigned gcd(unsigned a,unsigned b) decreases(b){return b==0u?a:gcd(b,a%b);}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("pure unsigned gcd(unsigned a, unsigned b)\n    decreases (b)\n{\n") !=
               std::string::npos);
}

CPPL_TEST(function_all_three_clause_kinds_use_expects_ensures_decreases_order) {
    const std::string input =
        "verified pure unsigned f(unsigned x) expects(x>0u) ensures(result>0u) decreases(x){return x;}\n";
    const std::string formatted = format_text(input);
    const std::size_t expects_pos = formatted.find("expects (");
    const std::size_t ensures_pos = formatted.find("ensures (");
    const std::size_t decreases_pos = formatted.find("decreases (");
    CPPL_CHECK(expects_pos != std::string::npos);
    CPPL_CHECK(ensures_pos != std::string::npos);
    CPPL_CHECK(decreases_pos != std::string::npos);
    CPPL_CHECK(expects_pos < ensures_pos);
    CPPL_CHECK(ensures_pos < decreases_pos);
    CPPL_CHECK(formatted.find("\n{\n", decreases_pos) != std::string::npos);
}

CPPL_TEST(contracted_function_declaration_ends_with_semicolon_without_inventing_a_brace) {
    const std::string input = "verified int f(int x) expects(x>=0) ensures(result>=0);\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK_EQ(formatted, "verified int f(int x)\n"
                             "    expects (x >= 0)\n"
                             "    ensures (result >= 0);\n");
}

CPPL_TEST(verified_pure_order_is_preserved) {
    const std::string input = "verified pure int f(int x) ensures(result==x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("verified pure int f(int x)\n") != std::string::npos);
}

CPPL_TEST(static_precedes_verified_modifier) {
    const std::string input = "static verified int f(int x) ensures(result==x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("static verified int f(int x)\n") != std::string::npos);
}

CPPL_TEST(inline_precedes_verified_pure_modifiers) {
    const std::string input = "inline verified pure int f(int x) ensures(result==x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("inline verified pure int f(int x)\n") != std::string::npos);
}

CPPL_TEST(constexpr_precedes_verified_pure_modifiers) {
    const std::string input = "constexpr verified pure int f(int x) ensures(result==x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("constexpr verified pure int f(int x)\n") != std::string::npos);
}

CPPL_TEST(consteval_precedes_verified_pure_modifiers) {
    const std::string input = "consteval verified pure int f(int x) ensures(result==x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("consteval verified pure int f(int x)\n") != std::string::npos);
}

// The formatter lexes the source as written, not the preprocessed text the
// compiler recognizes, so a macro that expands to `verified` is just an
// identifier here. The clauses are still laid out: which specifier happens to
// precede a contract must not decide whether it gets canonical form.
CPPL_TEST(macro_spelled_specifier_still_formats_the_contract) {
    const std::string input = "#define V verified\nV int f(int x) ensures(result==x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("V int f(int x)\n    ensures (result == x)\n") != std::string::npos);
}

CPPL_TEST(unrecognized_leading_specifier_still_formats_the_contract) {
    const std::string input = "inline int f(int x) ensures(result==x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("inline int f(int x)\n    ensures (result == x)\n") != std::string::npos);
}

CPPL_TEST(contract_with_no_specifier_at_all_still_formats) {
    const std::string input = "int f(int x) ensures(result==x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("int f(int x)\n    ensures (result == x)\n") != std::string::npos);
}

CPPL_TEST(noexcept_remains_in_ordinary_declarator_before_contract_clauses) {
    const std::string input = "verified int f(int x) noexcept ensures(result==x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("verified int f(int x) noexcept\n    ensures (result == x)\n") != std::string::npos);
}

CPPL_TEST(trailing_return_type_remains_before_contract_clauses) {
    const std::string input = "verified auto f(int x)->int ensures(result==x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("verified auto f(int x) -> int\n    ensures (result == x)\n") != std::string::npos);
}

CPPL_TEST(member_const_qualifier_remains_before_contract_clauses) {
    const std::string input = "struct S {\nverified int get() const ensures(result>=0){return 0;}\n};\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("    verified int get() const\n        ensures (result >= 0)\n    {\n") !=
               std::string::npos);
}

CPPL_TEST(member_reference_qualifier_remains_before_contract_clauses) {
    const std::string input = "struct S {\nverified int get() & ensures(result>=0){return 0;}\n};\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("get() &\n        ensures (result >= 0)\n") != std::string::npos);
}

CPPL_TEST(override_remains_before_contract_clauses) {
    const std::string input =
        "struct D {\nvirtual verified int f(int x) const override ensures(result==x){return x;}\n};\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("virtual verified int f(int x) const override\n        ensures (result == x)\n") !=
               std::string::npos);
}

CPPL_TEST(template_declaration_keeps_template_header_and_formats_contract) {
    const std::string input = "template<typename T>\nverified T identity(T x) ensures(result==x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("template <typename T>\nverified T identity(T x)\n    ensures (result == x)\n") !=
               std::string::npos);
}

CPPL_TEST(namespace_scope_indentation_is_respected_for_contract_clauses) {
    const std::string input = "namespace n {\nverified int f(int x) ensures(result==x){return x;}\n}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("verified int f(int x)\n    ensures (result == x)\n{\n") != std::string::npos);
}

CPPL_TEST(comments_inside_contract_predicate_are_preserved) {
    const std::string input = "verified int f(int x) ensures(/*same*/result==x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("/*same*/") != std::string::npos);
    CPPL_CHECK(formatted.find("\n    ensures (") != std::string::npos);
}

CPPL_TEST(line_comment_between_contract_clauses_is_preserved) {
    const std::string input = "verified int f(int x)\n"
                              "    expects (x >= 0)\n"
                              "    // normal return stays nonnegative\n"
                              "    ensures (result >= 0)\n"
                              "{\n"
                              "    return x;\n"
                              "}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("// normal return stays nonnegative") != std::string::npos);
    CPPL_CHECK(formatted.find("expects (x >= 0)") != std::string::npos);
    CPPL_CHECK(formatted.find("ensures (result >= 0)") != std::string::npos);
}

CPPL_TEST(attribute_stays_with_the_ordinary_declaration) {
    const std::string input = "[[nodiscard]] static verified int f(int x) ensures(result==x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("[[nodiscard]] static verified int f(int x)\n") != std::string::npos);
}

CPPL_TEST(pure_function_without_verified_still_uses_contract_layout) {
    const std::string input = "pure int f(int x) ensures(result==x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("pure int f(int x)\n    ensures (result == x)\n{\n") != std::string::npos);
}

CPPL_TEST(already_canonical_verified_function_is_a_no_op) {
    const std::string input =
        "verified int f(int x)\n    expects (x >= 0)\n    ensures (result >= 0)\n{\n    return x;\n}\n";
    const formatter::FormatResult result = document_result(input);
    CPPL_CHECK(result.ok);
    CPPL_CHECK(result.edits.empty());
}

// -----------------------------------------------------------------------------
// Loop clauses
// -----------------------------------------------------------------------------

CPPL_TEST(while_invariant_moves_to_its_own_indented_continuation_line) {
    const std::string input = "verified unsigned count() ensures(result==3u){\n"
                              "unsigned i=0u;\n"
                              "while(i<3u) invariant(i<=3u){++i;}\n"
                              "return i;\n"
                              "}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("while (i < 3u)\n        invariant (i <= 3u)\n    {\n") != std::string::npos);
}

CPPL_TEST(while_invariant_then_decreases_use_grammar_order) {
    const std::string input = "verified unsigned f(unsigned n) ensures(result==n){\n"
                              "while(n>0u) invariant(n>=0u) decreases(n){--n;}\n"
                              "return n;\n"
                              "}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("while (n > 0u)\n        invariant (n >= 0u)\n        decreases (n)\n    {\n") !=
               std::string::npos);
}

CPPL_TEST(for_loop_invariant_moves_to_its_own_line) {
    const std::string input = "verified unsigned f() ensures(result==3u){\n"
                              "unsigned n=0u;\n"
                              "for(unsigned i=0u;i<3u;++i) invariant(i<=3u){++n;}\n"
                              "return n;\n"
                              "}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("invariant (i <= 3u)\n    {") != std::string::npos);
}

CPPL_TEST(for_loop_invariant_then_decreases_are_separate_lines) {
    const std::string input = "verified unsigned f(unsigned n) ensures(result==0u){\n"
                              "for(;n>0u;--n) invariant(n>=0u) decreases(n){}\n"
                              "return n;\n"
                              "}\n";
    const std::string formatted = format_text(input);
    const std::size_t invariant = formatted.find("invariant (");
    const std::size_t decreases = formatted.find("decreases (", invariant);
    CPPL_CHECK(invariant != std::string::npos);
    CPPL_CHECK(decreases != std::string::npos);
    CPPL_CHECK(invariant < decreases);
}

CPPL_TEST(range_for_invariant_is_formatted_as_a_loop_clause) {
    const std::string input = "verified int f(int (&xs)[3]) ensures(result>=0){\n"
                              "for(int x:xs) invariant(x>=0){}\n"
                              "return 0;\n"
                              "}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("invariant (x >= 0)\n") != std::string::npos);
}

CPPL_TEST(do_loop_invariant_follows_do_header_and_precedes_body) {
    const std::string input = "verified int f(int n) ensures(result>=0){\n"
                              "do invariant(n>=0){--n;} while(n>0);\n"
                              "return n;\n"
                              "}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("do\n        invariant (n >= 0)\n    {\n") != std::string::npos);
}

CPPL_TEST(nested_loop_clause_indentation_tracks_nesting_depth) {
    const std::string input = "verified int f(int n) ensures(result>=0){\n"
                              "while(n>0) invariant(n>=0){\n"
                              "while(n>1) invariant(n>=1){--n;}\n"
                              "--n;\n"
                              "}\n"
                              "return n;\n"
                              "}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("while (n > 0)\n        invariant (n >= 0)\n    {\n") != std::string::npos);
    CPPL_CHECK(formatted.find("        while (n > 1)\n            invariant (n >= 1)\n        {\n") !=
               std::string::npos);
}

CPPL_TEST(loop_opening_brace_returns_to_loop_header_indentation_after_clauses) {
    const std::string input = "verified int f(int n) ensures(result>=0){while(n>0) invariant(n>=0){--n;}return n;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("    while (n > 0)\n        invariant (n >= 0)\n    {\n") != std::string::npos);
}

CPPL_TEST(ordinary_loop_without_cppl_clause_keeps_ordinary_clang_format_shape) {
    const std::string input = "void f(){while(true){break;}}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("while (true) {") != std::string::npos);
    CPPL_CHECK(formatted.find("\n        invariant (") == std::string::npos);
}

CPPL_TEST(already_canonical_loop_clause_layout_is_idempotent) {
    const std::string input = "verified int f(int n)\n"
                              "    ensures (result >= 0)\n"
                              "{\n"
                              "    while (n > 0)\n"
                              "        invariant (n >= 0)\n"
                              "    {\n"
                              "        --n;\n"
                              "    }\n"
                              "    return n;\n"
                              "}\n";
    const std::string once = format_text(input);
    const std::string twice = format_text(once);
    CPPL_CHECK_EQ(once, twice);
}

// SPEC: UNSAFE-001
// An unsafe block is laid out as the compound statement it delimits, with the
// keyword where the statement begins, and an unsafe declaration keeps its
// specifier in front of the return type.
CPPL_TEST(unsafe_blocks_and_declarations_are_laid_out_like_the_cpp_they_delimit) {
    const std::string input = "unsafe   unsigned read_device();\n"
                              "verified unsigned f(unsigned x)\n"
                              "    ensures (result == x)\n"
                              "{\n"
                              "    unsigned y = 0u;\n"
                              "  unsafe   {\n"
                              "y = read_device();\n"
                              "    }\n"
                              "    return x;\n"
                              "}\n";
    const std::string once = format_text(input);
    CPPL_CHECK(once.find("unsafe unsigned read_device();\n") != std::string::npos);
    CPPL_CHECK(once.find("\n    unsafe {\n        y = read_device();\n    }\n") != std::string::npos);
    CPPL_CHECK_EQ(format_text(once), once);
}

// SPEC: TERMINATION-004
// A lexicographic measure is one clause, laid out as one, its components
// separated as any comma-separated list is.
CPPL_TEST(a_lexicographic_measure_is_one_clause_on_functions_and_loops) {
    const std::string input = "verified unsigned f(unsigned m,unsigned n) ensures (result == 0u) decreases (m,n)\n"
                              "{\n"
                              "    unsigned r = m;\n"
                              "    while (r > 0u) invariant (r <= m) decreases (r,n) { --r; }\n"
                              "    return r;\n"
                              "}\n";
    const std::string once = format_text(input);
    CPPL_CHECK(once.find("\n    decreases (m, n)\n") != std::string::npos);
    CPPL_CHECK(once.find("\n        decreases (r, n)\n") != std::string::npos);
    CPPL_CHECK_EQ(format_text(once), once);
}

// SPEC: GHOST-001
// A ghost declaration is laid out as the declaration it prefixes, with the word
// where the statement begins.
CPPL_TEST(ghost_declarations_are_laid_out_like_the_declaration_they_prefix) {
    const std::string input = "verified unsigned f(unsigned x)\n"
                              "    ensures (result == x)\n"
                              "{\n"
                              "      ghost   unsigned seen=x;\n"
                              "    while (seen > x)\n"
                              "        invariant (seen >= x)\n"
                              "    {\n"
                              "  ghost bool   odd = (seen % 2u) == 1u;\n"
                              "    }\n"
                              "    return x;\n"
                              "}\n";
    const std::string once = format_text(input);
    CPPL_CHECK(once.find("\n    ghost unsigned seen = x;\n") != std::string::npos);
    CPPL_CHECK(once.find("\n        ghost bool odd = (seen % 2u) == 1u;\n") != std::string::npos);
    CPPL_CHECK_EQ(format_text(once), once);
}

// -----------------------------------------------------------------------------
// Refinements: where stays attached
// -----------------------------------------------------------------------------

CPPL_TEST(refinement_where_stays_attached_to_the_declaration) {
    const std::string input = "type NonNegative = int where (self >= 0);\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("type NonNegative = int where (self >= 0);") != std::string::npos);
    CPPL_CHECK(formatted.find("\n    where (") == std::string::npos);
}

CPPL_TEST(indexed_refinement_where_stays_attached) {
    const std::string input = "type Index(std::size_t n)=std::size_t where(self<n);\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("where (self < n);") != std::string::npos);
    CPPL_CHECK(formatted.find("\n    where (") == std::string::npos);
}

CPPL_TEST(refinement_predicate_with_conjunction_remains_one_where_predicate) {
    const std::string input = "type Percentage=int where(self>=0&&self<=100);\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(count_occurrences(formatted, "where") == 1);
    CPPL_CHECK(formatted.find("where (self >= 0 && self <= 100);") != std::string::npos);
}

CPPL_TEST(refinement_is_not_relocated_when_neighboring_contract_is_formatted) {
    const std::string input = "type Percentage = int where (self >= 0 && self <= 100);\n"
                              "verified Percentage half() ensures(result==50){return 50;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("type Percentage = int where (self >= 0 && self <= 100);\n") != std::string::npos);
    CPPL_CHECK(formatted.find("half()\n    ensures (result == 50)\n{") != std::string::npos);
}

CPPL_TEST(multiple_refinements_each_keep_where_attached) {
    const std::string input = "type NonNegative=int where(self>=0);\n"
                              "type Percentage=NonNegative where(self<=100);\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(count_occurrences(formatted, " where (") == 2);
    CPPL_CHECK(formatted.find("\n    where (") == std::string::npos);
}

CPPL_TEST(ordinary_cpp_identifier_named_where_is_not_treated_as_refinement_syntax) {
    const std::string input = "int where=1;\nint f(){return where;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("int where = 1;") != std::string::npos);
    CPPL_CHECK(formatted.find("\n    where (") == std::string::npos);
}

CPPL_TEST(already_canonical_refinement_is_a_no_op) {
    const std::string input = "type NonNegative = int where (self >= 0);\n";
    const formatter::FormatResult result = document_result(input);
    CPPL_CHECK(result.ok);
    CPPL_CHECK(result.edits.empty());
}

CPPL_TEST(unicode_comment_on_refinement_line_is_preserved) {
    const std::string input = "type Percentage=int where(self>=0&&self<=100); // procent — 0…100\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("procent — 0…100") != std::string::npos);
    CPPL_CHECK(formatted.find("where (self >= 0 && self <= 100)") != std::string::npos);
}
