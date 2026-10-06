// Formatter tests (formatter_test): proof declarations and proof arms, and
// contextual words that stay ordinary C++.

#include "cppl/formatter/format.hpp"
#include "cppl/testing/test.hpp"
#include "formatter_test_support.hpp"

#include <string>
#include <vector>

using namespace formatter_test_detail;

using namespace cppl;

// -----------------------------------------------------------------------------
// Proof declarations and proof arms
// -----------------------------------------------------------------------------

CPPL_TEST(proof_proves_clause_moves_to_its_own_line) {
    const std::string input = "proof p() proves(1==1){refl;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("proof p()\n    proves (1 == 1)\n{\n") != std::string::npos);
}

CPPL_TEST(already_canonical_proof_header_is_a_no_op) {
    const std::string input = "proof p()\n    proves (1 == 1)\n{\n    refl;\n}\n";
    const formatter::FormatResult result = document_result(input);
    CPPL_CHECK(result.ok);
    CPPL_CHECK(result.edits.empty());
}

CPPL_TEST(cases_arms_are_expanded_and_separated_by_one_blank_line) {
    const std::string input = "enum class Flag { on, off };\n"
                              "proof p(Flag f) proves(f==Flag::on||f==Flag::off){\n"
                              "cases f { Flag::on=>{refl;} Flag::off=>{refl;} }\n"
                              "}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("Flag::on => {\n") != std::string::npos);
    CPPL_CHECK(formatted.find("}\n\n        Flag::off => {") != std::string::npos);
}

// SPEC: CASE-017
// A split in a verified body lays its arms out as a proof body's does, and the
// statement after it keeps a line of its own at the split's indentation.
CPPL_TEST(a_split_on_a_runtime_path_lays_out_its_arms_and_keeps_the_next_statement_apart) {
    const std::string input = "enum class Flag { on, off };\n"
                              "verified int f(Flag f) ensures(result==0){\n"
                              "cases f { Flag::on=>{} Flag::off=>{} unnamed(v)=>{} } return 0;\n"
                              "}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("    cases f {\n        Flag::on => {\n        }\n\n        Flag::off => {") !=
               std::string::npos);
    CPPL_CHECK(formatted.find("        unnamed(v) => {\n        }\n    }\n    return 0;\n}") != std::string::npos);
    CPPL_CHECK_EQ(format_text(formatted), formatted);
}

// SPEC: CASE-017
// As the unbraced body of an `if`, a split is indented as any other statement
// there, and the statement after it returns to the `if`'s own level.
CPPL_TEST(a_split_as_an_unbraced_if_body_is_indented_as_its_body) {
    const std::string input = "enum class Flag { on, off };\n"
                              "verified int f(Flag f, bool b) ensures(result==0){\n"
                              "if (b)\n"
                              "cases f { Flag::on=>{} Flag::off=>{} unnamed(v)=>{} } return 0;\n"
                              "}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("    if (b)\n        cases f {\n            Flag::on => {\n") != std::string::npos);
    CPPL_CHECK(formatted.find("            }\n        }\n    return 0;\n}") != std::string::npos);
    CPPL_CHECK_EQ(format_text(formatted), formatted);
}

CPPL_TEST(proof_arm_binders_have_no_space_before_binding_parenthesis) {
    const std::string input = "proof p(unsigned n) proves(n==n){\n"
                              "induction n { zero=>{refl;} successor(pred)=>{refl;} }\n"
                              "}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("successor(pred) => {") != std::string::npos);
    CPPL_CHECK(formatted.find("successor (pred)") == std::string::npos);
}

CPPL_TEST(nested_cases_keep_each_arm_expanded) {
    const std::string input =
        "enum class Flag { on, off };\n"
        "proof p(Flag a,Flag b) proves(a==a){\n"
        "cases a { Flag::on=>{cases b { Flag::on=>{refl;} Flag::off=>{refl;} }} Flag::off=>{refl;} }\n"
        "}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(count_occurrences(formatted, "Flag::on => {") >= 2);
    CPPL_CHECK(formatted.find("\n\n") != std::string::npos);
}

CPPL_TEST(induction_arms_use_the_same_canonical_arm_layout) {
    const std::string input =
        "proof p(unsigned n) proves(n==n){induction n { zero=>{refl;} successor(pred)=>{refl;} }}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("zero => {\n") != std::string::npos);
    CPPL_CHECK(formatted.find("\n\n        successor(pred) => {\n") != std::string::npos);
}

CPPL_TEST(decompose_arm_uses_canonical_bindings_arrow_and_brace) {
    const std::string input =
        "#include <utility>\n"
        "proof p(std::pair<int, int> x) proves(true){decompose x { components(first,second)=>{refl;} }}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("components(first, second) => {") != std::string::npos);
}

CPPL_TEST(variant_alternative_arm_keeps_template_index_attached_to_label) {
    const std::string input =
        "#include <variant>\n"
        "proof p(std::variant<int> x) proves(true){cases x { alternative<0>(value)=>{refl;} valueless=>{refl;} }}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("alternative<0>(value) => {") != std::string::npos);
}

CPPL_TEST(valueless_residual_arm_has_no_invented_binder_parentheses) {
    const std::string input = "#include <variant>\n"
                              "proof p(std::variant<int> x) proves(true){cases x { valueless=>{refl;} }}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("valueless => {") != std::string::npos);
    CPPL_CHECK(formatted.find("valueless()") == std::string::npos);
}

CPPL_TEST(unnamed_enum_residual_arm_uses_binder_form) {
    const std::string input = "enum class E { known };\n"
                              "proof p(E e) proves(true){cases e { unnamed(value)=>{refl;} }}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("unnamed(value) => {") != std::string::npos);
}

CPPL_TEST(primitive_proof_commands_remain_statements_not_call_syntax) {
    const std::string input = "proof p(int x) proves(x==x){refl;rewrite eq;exact h;apply lemma;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("refl;") != std::string::npos);
    CPPL_CHECK(formatted.find("rewrite eq;") != std::string::npos);
    CPPL_CHECK(formatted.find("exact h;") != std::string::npos);
    CPPL_CHECK(formatted.find("apply lemma;") != std::string::npos);
    CPPL_CHECK(formatted.find("refl()") == std::string::npos);
}

CPPL_TEST(proof_comments_are_preserved) {
    const std::string input = "proof p() proves(1==1){// closes by reflexivity\nrefl;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("// closes by reflexivity") != std::string::npos);
}

CPPL_TEST(comments_between_proof_arms_are_preserved) {
    // A comment beside an arm, rather than inside one, belongs to no arm's
    // body. Rebuilding the arm block from its arms alone used to drop it
    // silently, which is data loss, and a comment explaining why a case is
    // omitted is exactly what an author writes above `omit`.
    const std::string input = "enum class S { a, b };\n"
                              "proof p(S s) proves(true){cases s { // the partition\n"
                              "// first\n"
                              "S::a=>{refl;} // after a\n"
                              "\n"
                              "   // why b cannot occur\n"
                              "omit S::b by contradiction h; // see the premise\n"
                              "/* the rest */ unnamed(v)=>{refl;}\n"
                              "// trailing\n"
                              "}}\n";
    const std::string once = format_text(input);
    CPPL_CHECK(once.find("cases s { // the partition\n") != std::string::npos);
    CPPL_CHECK(once.find("        // first\n        S::a => {\n") != std::string::npos);
    CPPL_CHECK(once.find("        } // after a\n") != std::string::npos);
    CPPL_CHECK(once.find("\n\n        // why b cannot occur\n        omit S::b by contradiction h;") !=
               std::string::npos);
    CPPL_CHECK(once.find("omit S::b by contradiction h; // see the premise\n") != std::string::npos);
    CPPL_CHECK(once.find("\n\n        /* the rest */\n        unnamed(v) => {\n") != std::string::npos);
    CPPL_CHECK(once.find("        }\n\n        // trailing\n    }\n") != std::string::npos);
    // Laying the comments out is a fixed point, like the rest of the block.
    CPPL_CHECK_EQ(format_text(once), once);
}

CPPL_TEST(proof_arm_formatting_is_idempotent) {
    const std::string input = "enum class Flag { on, off };\n"
                              "proof p(Flag f) proves(true){cases f { Flag::on=>{refl;} Flag::off=>{refl;} }}\n";
    const std::string once = format_text(input);
    const std::string twice = format_text(once);
    CPPL_CHECK_EQ(once, twice);
}

CPPL_TEST(class_scope_proof_header_and_body_indent_relative_to_class) {
    const std::string input = "struct S {\nproof p() proves(1==1){refl;}\n};\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("    proof p()\n        proves (1 == 1)\n    {\n") != std::string::npos);
}

// -----------------------------------------------------------------------------
// Contextual-word safety / C++ superset behavior
// -----------------------------------------------------------------------------

CPPL_TEST(contextual_words_remain_valid_ordinary_cpp_identifiers) {
    const std::string input = "int law=1; int proof=2; int proves=3; int pure=4; int verified=5;\n"
                              "int ghost=6; int unsafe=7; int trusted=8; int type=9; int where=10;\n"
                              "int expects=11; int ensures=12; int decreases=13; int invariant=14;\n"
                              "int forall=15; int exists=16;\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("int law = 1;") != std::string::npos);
    CPPL_CHECK(formatted.find("int expects = 11;") != std::string::npos);
    CPPL_CHECK(formatted.find("int invariant = 14;") != std::string::npos);
}

CPPL_TEST(ordinary_cpp_functions_named_expects_ensures_and_proves_are_not_clause_syntax) {
    const std::string input =
        "int expects(int x){return x;} int ensures(int x){return x;} int proves(int x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("int expects(int x) {") != std::string::npos);
    CPPL_CHECK(formatted.find("int ensures(int x) {") != std::string::npos);
    CPPL_CHECK(formatted.find("int proves(int x) {") != std::string::npos);
}

CPPL_TEST(ordinary_cpp_type_named_ghost_is_not_cppl_ghost_declaration) {
    const std::string input = "struct ghost{int value;};\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("struct ghost {") != std::string::npos);
}

CPPL_TEST(member_named_result_outside_postcondition_remains_ordinary_cpp) {
    const std::string input = "struct S{int result;};\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("int result;") != std::string::npos);
}

CPPL_TEST(function_named_old_outside_postcondition_remains_ordinary_cpp) {
    const std::string input = "int old(int x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("int old(int x) {") != std::string::npos);
}
