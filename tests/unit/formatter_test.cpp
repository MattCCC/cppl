// Production conformance tests for cppl::formatter, the one canonical-formatting
// engine shared by cppl-format and cppl-lsp.
//
// Normative canonical rules covered here:
//   * expects / ensures / decreases / invariant / proves use exactly one space
//     before '(' and each starts on its own continuation line;
//   * clause order is the grammar order for each construct;
//   * a contracted function/loop/proof body's opening '{' is on the following
//     line at the declaration/header indentation;
//   * refinement `where (P)` stays attached to its declaration;
//   * proof arms use `label(bindings) => {` and one blank line between arms;
//   * ordinary C++ remains ordinary C++ and contextual C++L words are not
//     globally reserved;
//   * document/range/on-type formatting share one deterministic, idempotent
//     formatting engine;
//   * returned edits are sorted, non-overlapping, and expressed in ORIGINAL
//     byte offsets.
//
// These tests intentionally include grammar-valid surfaces that may expose
// currently unimplemented formatter behavior. Such failures are conformance
// failures, not reasons to weaken the tests.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/formatter/format.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace cppl;

namespace {

[[noreturn]] void fail_test(const std::string& message) {
    ::cppl::testing::fail(__FILE__, __LINE__, message);
}

void check_edits_well_formed(const std::string& text, const std::vector<formatter::FormatEdit>& edits) {
    std::size_t previous_end = 0;
    bool first = true;

    for (const formatter::FormatEdit& edit : edits) {
        if (edit.span.offset > text.size()) {
            fail_test("format edit starts past end of original input");
        }
        if (edit.span.length > text.size() - edit.span.offset) {
            fail_test("format edit extends past end of original input");
        }
        if (!first && edit.span.offset < previous_end) {
            fail_test("format edits are not sorted and non-overlapping");
        }

        previous_end = edit.span.offset + edit.span.length;
        first = false;
    }
}

std::string apply_edits(const std::string& text, const std::vector<formatter::FormatEdit>& edits) {
    check_edits_well_formed(text, edits);

    std::string result;
    std::size_t cursor = 0;
    for (const formatter::FormatEdit& edit : edits) {
        result.append(text, cursor, edit.span.offset - cursor);
        result += edit.replacement;
        cursor = edit.span.offset + edit.span.length;
    }
    result.append(text, cursor, text.size() - cursor);
    return result;
}

formatter::FormatRequest make_request(const std::string& text) {
    formatter::FormatRequest request;
    request.text = text;
    return request;
}

formatter::FormatResult document_result(const std::string& text) {
    formatter::FormatRequest request = make_request(text);
    formatter::FormatResult result = formatter::format_document(request);
    if (result.ok) {
        check_edits_well_formed(text, result.edits);
    }
    return result;
}

std::string format_text(const std::string& text) {
    const formatter::FormatResult result = document_result(text);
    CPPL_CHECK(result.ok);
    return apply_edits(text, result.edits);
}

std::string format_ranges_text(const std::string& text, const std::vector<source::ByteSpan>& ranges) {
    formatter::FormatRequest request = make_request(text);
    const formatter::FormatResult result = formatter::format_ranges(request, ranges);
    CPPL_CHECK(result.ok);
    check_edits_well_formed(text, result.edits);
    return apply_edits(text, result.edits);
}

std::string format_on_type_text(const std::string& text, std::size_t position, const std::string& trigger) {
    formatter::FormatRequest request = make_request(text);
    const formatter::FormatResult result = formatter::format_on_type(request, position, trigger);
    CPPL_CHECK(result.ok);
    check_edits_well_formed(text, result.edits);
    return apply_edits(text, result.edits);
}

std::vector<diagnostics::Diagnostic> style_diagnostics(const std::string& text) {
    const frontend::TokenStream tokens = frontend::lex(text, "style.cpp");
    diagnostics::Engine engine;
    const frontend::Syntax syntax = frontend::recognize(tokens, engine);
    return formatter::check_style(tokens, syntax);
}

bool has_style_message(const std::vector<diagnostics::Diagnostic>& diagnostics, const std::string& needle) {
    for (const diagnostics::Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.category == diagnostics::Category::Style &&
            diagnostic.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::size_t count_occurrences(const std::string& text, const std::string& needle) {
    if (needle.empty()) {
        return 0;
    }

    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

std::string read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        fail_test("could not read '" + path + "'");
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

} // namespace

// -----------------------------------------------------------------------------
// Engine contract and edit integrity
// -----------------------------------------------------------------------------

CPPL_TEST(empty_document_formats_successfully_without_edits) {
    const formatter::FormatResult result = document_result("");
    CPPL_CHECK(result.ok);
    CPPL_CHECK(result.edits.empty());
}

CPPL_TEST(whitespace_only_document_formats_successfully) {
    const formatter::FormatResult result = document_result("\n\n");
    CPPL_CHECK(result.ok);
    check_edits_well_formed("\n\n", result.edits);
}

CPPL_TEST(format_document_is_equivalent_to_format_ranges_with_empty_range_vector) {
    const std::string input = "verified int f(int x) ensures (result == x) { return x; }\n";

    formatter::FormatRequest request = make_request(input);
    const formatter::FormatResult document = formatter::format_document(request);
    const formatter::FormatResult ranges = formatter::format_ranges(request, {});

    CPPL_CHECK(document.ok);
    CPPL_CHECK(ranges.ok);
    CPPL_CHECK_EQ(apply_edits(input, document.edits), apply_edits(input, ranges.edits));
}

CPPL_TEST(format_result_edits_are_sorted_non_overlapping_and_within_original_buffer) {
    const std::string input = "verified int f(int x) ensures(result==x){int   y=x;return y;}\n"
                              "law l(int x) proves(x==x);\n";
    const formatter::FormatResult result = document_result(input);
    CPPL_CHECK(result.ok);
    check_edits_well_formed(input, result.edits);
}

CPPL_TEST(already_canonical_ordinary_cpp_is_a_no_op) {
    const std::string input = "int add(int a, int b) {\n    return a + b;\n}\n";
    const formatter::FormatResult result = document_result(input);
    CPPL_CHECK(result.ok);
    CPPL_CHECK(result.edits.empty());
}

CPPL_TEST(formatting_twice_equals_formatting_once_for_mixed_cpp_and_cppl) {
    const std::string input = "verified int f(int x) expects(x>=0) ensures(result>=0){\n"
                              "int   y=x;\n"
                              "while(y<3) invariant(y<=3){++y;}\n"
                              "return y;\n"
                              "}\n"
                              "law nonnegative(int x) proves(x>=x);\n";
    const std::string once = format_text(input);
    const std::string twice = format_text(once);
    CPPL_CHECK_EQ(once, twice);
}

CPPL_TEST(virtual_path_does_not_change_canonical_bytes) {
    const std::string input = "verified int f(int x) ensures(result==x){return x;}\n";

    formatter::FormatRequest first = make_request(input);
    first.virtual_path = "one/path/source.cppl";
    formatter::FormatRequest second = make_request(input);
    second.virtual_path = "another/path/source.cpp";

    const formatter::FormatResult a = formatter::format_document(first);
    const formatter::FormatResult b = formatter::format_document(second);
    CPPL_CHECK(a.ok);
    CPPL_CHECK(b.ok);
    CPPL_CHECK_EQ(apply_edits(input, a.edits), apply_edits(input, b.edits));
}

CPPL_TEST(utf8_bytes_are_preserved_around_cppl_formatting) {
    const std::string input = "// Zażółć gęślą jaźń — Ελληνικά — 日本語\n"
                              "verified int f(int x) ensures(result==x){return x;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("Zażółć gęślą jaźń — Ελληνικά — 日本語") != std::string::npos);
    CPPL_CHECK(formatted.find("\n    ensures (result == x)\n{") != std::string::npos);
}

CPPL_TEST(cppl_words_inside_comments_and_strings_are_not_reinterpreted) {
    const std::string input = "// expects(x) ensures(y) proves(z) invariant(q) decreases(n)\n"
                              "const char* s = \"law proof verified pure where expects ensures proves\";\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("// expects(x) ensures(y) proves(z) invariant(q) decreases(n)") != std::string::npos);
    CPPL_CHECK(formatted.find("law proof verified pure where expects ensures proves") != std::string::npos);
    CPPL_CHECK(formatted.find("\n    expects (") == std::string::npos);
}

CPPL_TEST(cppl_words_inside_raw_strings_are_not_reinterpreted) {
    const std::string input = "const char* text = R\"tag(law x() proves(false); expects(x) { })tag\";\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("R\"tag(law x() proves(false); expects(x) { })tag\"") != std::string::npos);
}

CPPL_TEST(preprocessor_text_with_contextual_words_is_preserved_as_preprocessor_text) {
    const std::string input = "#define expects(x) x\n"
                              "#define LAW_TEXT \"law proof proves\"\n"
                              "int value = expects(1);\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("#define expects(x) x") != std::string::npos);
    CPPL_CHECK(formatted.find("#define LAW_TEXT \"law proof proves\"") != std::string::npos);
}

CPPL_TEST(syntax_fix_edits_if_any_obey_original_byte_edit_contract) {
    const std::string input = "pure verified int f(int x) ensures(result==x){return x;}\n";
    const formatter::FormatRequest request = make_request(input);
    const std::vector<formatter::SyntaxFix> fixes = formatter::syntax_fixes(request);
    for (const formatter::SyntaxFix& fix : fixes) {
        CPPL_CHECK(!fix.title.empty());
        check_edits_well_formed(input, fix.edits);
    }
}

CPPL_TEST(canonical_source_has_no_semantics_migration_fix_requirement) {
    const std::string input = "verified pure int f(int x)\n    ensures (result == x)\n{\n    return x;\n}\n";
    const formatter::FormatRequest request = make_request(input);
    const std::vector<formatter::SyntaxFix> fixes = formatter::syntax_fixes(request);
    CPPL_CHECK(fixes.empty());
}

CPPL_TEST(explicit_nonexistent_clang_format_path_fails_closed) {
    formatter::FormatRequest request = make_request("int x = 0;\n");
    request.clang_format = "/__cppl_test__/definitely-not-a-clang-format-binary";
    const formatter::FormatResult result = formatter::format_document(request);
    CPPL_CHECK(!result.ok);
    CPPL_CHECK(result.edits.empty());
}

CPPL_TEST(format_ranges_covering_entire_buffer_matches_document_formatting) {
    const std::string input = "verified int f(int x) ensures(result==x){return x;}\n";
    formatter::FormatRequest request = make_request(input);
    const formatter::FormatResult document = formatter::format_document(request);
    const formatter::FormatResult ranged = formatter::format_ranges(request, {source::ByteSpan{0, input.size()}});
    CPPL_CHECK(document.ok);
    CPPL_CHECK(ranged.ok);
    CPPL_CHECK_EQ(apply_edits(input, document.edits), apply_edits(input, ranged.edits));
}

CPPL_TEST(reversed_range_order_is_deterministic) {
    const std::string input = "void f(){\n    int   a=1;\n    int   b=2;\n}\n";
    const std::size_t a = input.find("int   a");
    const std::size_t b = input.find("int   b");
    const std::string forward = format_ranges_text(input, {source::ByteSpan{a, 1}, source::ByteSpan{b, 1}});
    const std::string reverse = format_ranges_text(input, {source::ByteSpan{b, 1}, source::ByteSpan{a, 1}});
    CPPL_CHECK_EQ(forward, reverse);
}

CPPL_TEST(duplicate_requested_ranges_do_not_duplicate_output_edits) {
    const std::string input = "verified int f(int x) ensures(result==x){return x;}\n";
    const std::size_t position = input.find("result==x");
    formatter::FormatRequest request = make_request(input);
    const formatter::FormatResult single = formatter::format_ranges(request, {source::ByteSpan{position, 1}});
    const formatter::FormatResult duplicate =
        formatter::format_ranges(request, {source::ByteSpan{position, 1}, source::ByteSpan{position, 1}});
    CPPL_CHECK(single.ok);
    CPPL_CHECK(duplicate.ok);
    CPPL_CHECK_EQ(apply_edits(input, single.edits), apply_edits(input, duplicate.edits));
}

CPPL_TEST(zero_length_range_at_end_of_buffer_is_safe) {
    const std::string input = "int x = 0;\n";
    formatter::FormatRequest request = make_request(input);
    const formatter::FormatResult result = formatter::format_ranges(request, {source::ByteSpan{input.size(), 0}});
    if (result.ok) {
        check_edits_well_formed(input, result.edits);
    } else {
        CPPL_CHECK(result.edits.empty());
    }
}

CPPL_TEST(on_type_position_at_end_of_buffer_is_safe) {
    const std::string input = "int   x=0;\n";
    formatter::FormatRequest request = make_request(input);
    const formatter::FormatResult result = formatter::format_on_type(request, input.size(), "\n");
    if (result.ok) {
        check_edits_well_formed(input, result.edits);
    } else {
        CPPL_CHECK(result.edits.empty());
    }
}

CPPL_TEST(formatter_never_silently_migrates_pure_verified_to_verified_pure) {
    const std::string input = "pure verified int f(int x) ensures(result==x){return x;}\n";
    const formatter::FormatResult result = document_result(input);
    if (result.ok) {
        const std::string formatted = apply_edits(input, result.edits);
        CPPL_CHECK(formatted.find("pure verified") != std::string::npos);
        CPPL_CHECK(formatted.find("verified pure") == std::string::npos);
    }
}

CPPL_TEST(formatter_never_silently_merges_repeated_expects_clauses) {
    const std::string input = "verified int f(int x) expects(x>=0) expects(x<=10) ensures(result==x){return x;}\n";
    const formatter::FormatResult result = document_result(input);
    if (result.ok) {
        const std::string formatted = apply_edits(input, result.edits);
        CPPL_CHECK(count_occurrences(formatted, "expects") == 2);
    }
}

CPPL_TEST(formatter_never_rewrites_illegal_function_proves_into_ensures) {
    const std::string input = "verified int f(int x) proves(x==x){return x;}\n";
    const formatter::FormatResult result = document_result(input);
    if (result.ok) {
        const std::string formatted = apply_edits(input, result.edits);
        CPPL_CHECK(count_occurrences(formatted, "proves") == 1);
        CPPL_CHECK(count_occurrences(formatted, "ensures") == 0);
    }
}

CPPL_TEST(formatter_never_rewrites_illegal_law_ensures_into_proves) {
    const std::string input = "law l(int x) ensures(x==x);\n";
    const formatter::FormatResult result = document_result(input);
    if (result.ok) {
        const std::string formatted = apply_edits(input, result.edits);
        CPPL_CHECK(count_occurrences(formatted, "ensures") == 1);
        CPPL_CHECK(count_occurrences(formatted, "proves") == 0);
    }
}

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

// -----------------------------------------------------------------------------
// Range formatting
// -----------------------------------------------------------------------------

CPPL_TEST(range_inside_clause_predicate_expands_to_complete_clause_unit) {
    const std::string input = "verified int f(int x) ensures(result>=0){return x;}\n";
    const std::size_t position = input.find("result>=0") + 2;
    const std::string formatted = format_ranges_text(input, {source::ByteSpan{position, 1}});
    CPPL_CHECK(formatted.find("\n    ensures (result >= 0)\n{") != std::string::npos);
}

CPPL_TEST(range_touching_clause_keyword_expands_to_complete_clause_unit) {
    const std::string input = "verified int f(int x) ensures(result>=0){return x;}\n";
    const std::size_t position = input.find("ensures") + 1;
    const std::string formatted = format_ranges_text(input, {source::ByteSpan{position, 2}});
    CPPL_CHECK(formatted.find("\n    ensures (result >= 0)\n{") != std::string::npos);
}

CPPL_TEST(zero_length_range_inside_clause_is_safe_and_clause_scoped) {
    const std::string input = "verified int f(int x) ensures(result>=0){return x;}\n";
    const std::size_t position = input.find("result>=0");
    const std::string formatted = format_ranges_text(input, {source::ByteSpan{position, 0}});
    CPPL_CHECK(formatted.find("\n    ensures (result >= 0)\n{") != std::string::npos);
}

CPPL_TEST(range_inside_ordinary_cpp_line_formats_that_line_only) {
    const std::string input = "verified int f(int x) ensures(result>=0){\n"
                              "    int   y   =   x;\n"
                              "    return y;\n"
                              "}\n";
    const std::size_t line = input.find("int   y");
    const std::string formatted = format_ranges_text(input, {source::ByteSpan{line, 3}});
    CPPL_CHECK(formatted.find("int y = x;") != std::string::npos);
    CPPL_CHECK(formatted.find("f(int x) ensures(result>=0){") != std::string::npos);
}

CPPL_TEST(range_in_one_function_does_not_reformat_unrelated_contract_elsewhere) {
    const std::string input = "verified int f(int x) ensures(result>=0){\n"
                              "    int   y=x;\n"
                              "    return y;\n"
                              "}\n"
                              "verified int g(int x) ensures(result>=0){return x;}\n";
    const std::size_t position = input.find("int   y");
    const std::string formatted = format_ranges_text(input, {source::ByteSpan{position, 1}});
    CPPL_CHECK(formatted.find("int y = x;") != std::string::npos);
    CPPL_CHECK(formatted.find("g(int x) ensures(result>=0){return x;}") != std::string::npos);
}

CPPL_TEST(two_disjoint_ordinary_cpp_ranges_format_both_and_nothing_between_them) {
    const std::string input = "void f(){\n"
                              "    int   a=1;\n"
                              "    int   untouched=2;\n"
                              "    int   b=3;\n"
                              "}\n";
    const std::size_t a = input.find("int   a");
    const std::size_t b = input.find("int   b");
    const std::string formatted = format_ranges_text(input, {source::ByteSpan{a, 1}, source::ByteSpan{b, 1}});
    CPPL_CHECK(formatted.find("int a = 1;") != std::string::npos);
    CPPL_CHECK(formatted.find("int b = 3;") != std::string::npos);
    CPPL_CHECK(formatted.find("int   untouched=2;") != std::string::npos);
}

CPPL_TEST(two_disjoint_clause_ranges_format_both_selected_clauses) {
    const std::string input = "verified int f(int x) ensures(result==x){return x;}\n"
                              "verified int g(int x) ensures(result==x){return x;}\n";
    const std::size_t first = input.find("result==x");
    const std::size_t second = input.find("result==x", first + 1);
    const std::string formatted = format_ranges_text(input, {source::ByteSpan{first, 1}, source::ByteSpan{second, 1}});
    CPPL_CHECK(count_occurrences(formatted, "\n    ensures (result == x)\n{") == 2);
}

CPPL_TEST(overlapping_requested_ranges_never_produce_overlapping_output_edits) {
    const std::string input = "verified int f(int x) ensures(result==x){return x;}\n";
    const std::size_t clause = input.find("ensures");
    formatter::FormatRequest request = make_request(input);
    const formatter::FormatResult result =
        formatter::format_ranges(request, {source::ByteSpan{clause, 5}, source::ByteSpan{clause + 2, 10}});
    CPPL_CHECK(result.ok);
    check_edits_well_formed(input, result.edits);
}

CPPL_TEST(range_inside_refinement_formats_the_refinement_declaration_without_turning_where_into_contract_line) {
    const std::string input = "type Percentage=int where(self>=0&&self<=100);\n"
                              "verified int f(int x) ensures(result==x){return x;}\n";
    const std::size_t position = input.find("self>=0");
    const std::string formatted = format_ranges_text(input, {source::ByteSpan{position, 1}});
    CPPL_CHECK(formatted.find("where (self >= 0 && self <= 100)") != std::string::npos);
    CPPL_CHECK(formatted.find("\n    where (") == std::string::npos);
    CPPL_CHECK(formatted.find("f(int x) ensures(result==x){return x;}") != std::string::npos);
}

CPPL_TEST(range_inside_comment_line_does_not_reinterpret_cppl_words_in_comment) {
    const std::string input = "// ensures(result==x) proves(x==x)\n"
                              "verified int f(int x) ensures(result==x){return x;}\n";
    const std::size_t position = input.find("ensures");
    const std::string formatted = format_ranges_text(input, {source::ByteSpan{position, 1}});
    CPPL_CHECK(formatted.find("// ensures(result==x) proves(x==x)") != std::string::npos);
    CPPL_CHECK(formatted.find("f(int x) ensures(result==x){return x;}") != std::string::npos);
}

CPPL_TEST(range_offsets_are_byte_offsets_even_when_utf8_precedes_selected_region) {
    const std::string input = "// ąęźć 日本語\n"
                              "verified int f(int x) ensures(result==x){return x;}\n";
    const std::size_t position = input.find("result==x");
    const std::string formatted = format_ranges_text(input, {source::ByteSpan{position, 1}});
    CPPL_CHECK(formatted.find("// ąęźć 日本語") != std::string::npos);
    CPPL_CHECK(formatted.find("\n    ensures (result == x)\n{") != std::string::npos);
}

CPPL_TEST(range_at_start_of_clause_does_not_require_nonzero_prefix_context) {
    const std::string input = "verified int f(int x) ensures(result==x){return x;}\n";
    const std::size_t position = input.find("ensures");
    const std::string formatted = format_ranges_text(input, {source::ByteSpan{position, 0}});
    CPPL_CHECK(formatted.find("\n    ensures (result == x)\n{") != std::string::npos);
}

CPPL_TEST(range_formatting_is_idempotent_for_the_same_selected_region) {
    const std::string input = "verified int f(int x) ensures(result==x){return x;}\n";
    const std::size_t position = input.find("result==x");
    const std::string once = format_ranges_text(input, {source::ByteSpan{position, 1}});
    const std::size_t second_position = once.find("result == x");
    const std::string twice = format_ranges_text(once, {source::ByteSpan{second_position, 1}});
    CPPL_CHECK_EQ(once, twice);
}

// -----------------------------------------------------------------------------
// On-type formatting
// -----------------------------------------------------------------------------

CPPL_TEST(on_type_inside_ensures_clause_formats_only_the_smallest_safe_clause_unit) {
    const std::string input = "verified int f(int x) ensures(result>=0){return x;}\n"
                              "verified int g(int x) ensures(result>=0){return x;}\n";
    const std::size_t position = input.find("result>=0") + 3;
    const std::string formatted = format_on_type_text(input, position, ")");
    CPPL_CHECK(formatted.find("f(int x)\n    ensures (result >= 0)\n{") != std::string::npos);
    CPPL_CHECK(formatted.find("g(int x) ensures(result>=0){return x;}") != std::string::npos);
}

CPPL_TEST(on_type_inside_expects_clause_formats_that_clause_unit) {
    const std::string input = "verified int f(int x) expects(x>=0) ensures(result>=0){return x;}\n";
    const std::size_t position = input.find("x>=0") + 1;
    const std::string formatted = format_on_type_text(input, position, ")");
    CPPL_CHECK(formatted.find("\n    expects (x >= 0)") != std::string::npos);
}

CPPL_TEST(on_type_inside_loop_invariant_formats_the_loop_clause_unit) {
    const std::string input = "verified int f(int n) ensures(result>=0){while(n>0) invariant(n>=0){--n;}return n;}\n";
    const std::size_t position = input.find("n>=0", input.find("invariant")) + 1;
    const std::string formatted = format_on_type_text(input, position, ")");
    CPPL_CHECK(formatted.find("invariant (n >= 0)") != std::string::npos);
}

CPPL_TEST(on_type_inside_ordinary_cpp_line_formats_only_that_line) {
    const std::string input = "verified int f(int x) ensures(result>=0){\n"
                              "    int   y=x;\n"
                              "    return y;\n"
                              "}\n";
    const std::size_t position = input.find("y=x") + 1;
    const std::string formatted = format_on_type_text(input, position, ";");
    CPPL_CHECK(formatted.find("int y = x;") != std::string::npos);
    CPPL_CHECK(formatted.find("f(int x) ensures(result>=0){") != std::string::npos);
}

CPPL_TEST(on_type_in_first_ordinary_line_does_not_reformat_later_contract) {
    const std::string input = "int   x=1;\nverified int f(int y) ensures(result==y){return y;}\n";
    const std::size_t position = input.find("x=1") + 1;
    const std::string formatted = format_on_type_text(input, position, ";");
    CPPL_CHECK(formatted.find("int x = 1;") != std::string::npos);
    CPPL_CHECK(formatted.find("f(int y) ensures(result==y){return y;}") != std::string::npos);
}

CPPL_TEST(on_type_mid_token_edits_never_escape_the_smallest_safe_unit) {
    const std::string input = "verified int f(int x) ensures(result>=0){return x;}\n";
    const std::size_t position = input.find("result") + 2;
    formatter::FormatRequest request = make_request(input);
    const formatter::FormatResult result = formatter::format_on_type(request, position, "x");
    CPPL_CHECK(result.ok);
    check_edits_well_formed(input, result.edits);

    const std::size_t clause_begin = input.find("ensures");
    const std::size_t clause_end = input.find('{', clause_begin);
    for (const formatter::FormatEdit& edit : result.edits) {
        CPPL_CHECK(edit.span.offset >= clause_begin);
        CPPL_CHECK(edit.span.offset + edit.span.length <= clause_end + 1);
    }
}

CPPL_TEST(on_type_empty_document_returns_no_edits) {
    formatter::FormatRequest request = make_request("");
    const formatter::FormatResult result = formatter::format_on_type(request, 0, "\n");
    CPPL_CHECK(result.ok);
    CPPL_CHECK(result.edits.empty());
}

CPPL_TEST(on_type_byte_position_remains_correct_after_utf8_prefix) {
    const std::string input = "// Łódź 日本語\nverified int f(int x) ensures(result==x){return x;}\n";
    const std::size_t position = input.find("result==x") + 2;
    const std::string formatted = format_on_type_text(input, position, ")");
    CPPL_CHECK(formatted.find("Łódź 日本語") != std::string::npos);
    CPPL_CHECK(formatted.find("\n    ensures (result == x)\n{") != std::string::npos);
}

CPPL_TEST(on_type_inside_refinement_keeps_where_attached) {
    const std::string input = "type Percentage=int where(self>=0&&self<=100);\n";
    const std::size_t position = input.find("self>=0") + 2;
    const std::string formatted = format_on_type_text(input, position, ")");
    CPPL_CHECK(formatted.find("where (self >= 0 && self <= 100)") != std::string::npos);
    CPPL_CHECK(formatted.find("\n    where (") == std::string::npos);
}

CPPL_TEST(on_type_formatting_is_idempotent) {
    const std::string input = "verified int f(int x) ensures(result==x){return x;}\n";
    const std::size_t position = input.find("result==x") + 2;
    const std::string once = format_on_type_text(input, position, ")");
    const std::size_t second_position = once.find("result == x") + 2;
    const std::string twice = format_on_type_text(once, second_position, ")");
    CPPL_CHECK_EQ(once, twice);
}

// -----------------------------------------------------------------------------
// Style diagnostics
// -----------------------------------------------------------------------------

CPPL_TEST(check_style_flags_inline_expects_clause) {
    const auto diagnostics = style_diagnostics("verified int f(int x) expects (x >= 0) { return x; }\n");
    CPPL_CHECK(has_style_message(diagnostics, "own continuation line"));
}

CPPL_TEST(check_style_flags_inline_ensures_clause) {
    const auto diagnostics = style_diagnostics("verified int f(int x) ensures (result >= 0) { return x; }\n");
    CPPL_CHECK(has_style_message(diagnostics, "own continuation line"));
}

CPPL_TEST(check_style_flags_inline_proves_clause) {
    const auto diagnostics = style_diagnostics("law l(int x) proves (x == x);\n");
    CPPL_CHECK(has_style_message(diagnostics, "own continuation line"));
}

CPPL_TEST(check_style_flags_inline_invariant_clause) {
    const auto diagnostics = style_diagnostics("verified int f(int n)\n    ensures (result >= 0)\n{\n    while (n > 0) "
                                               "invariant (n >= 0) { --n; }\n    return n;\n}\n");
    CPPL_CHECK(has_style_message(diagnostics, "own continuation line"));
}

CPPL_TEST(check_style_flags_inline_decreases_clause) {
    const auto diagnostics = style_diagnostics("pure unsigned f(unsigned n) decreases (n) { return n; }\n");
    CPPL_CHECK(has_style_message(diagnostics, "own continuation line"));
}

CPPL_TEST(check_style_flags_missing_space_before_expects_parenthesis) {
    const auto diagnostics = style_diagnostics("verified int f(int x)\n    expects(x >= 0)\n{\n    return x;\n}\n");
    CPPL_CHECK(has_style_message(diagnostics, "one space before"));
}

CPPL_TEST(check_style_flags_missing_space_before_ensures_parenthesis) {
    const auto diagnostics =
        style_diagnostics("verified int f(int x)\n    ensures(result >= 0)\n{\n    return x;\n}\n");
    CPPL_CHECK(has_style_message(diagnostics, "one space before"));
}

CPPL_TEST(check_style_flags_missing_space_before_proves_parenthesis) {
    const auto diagnostics = style_diagnostics("law l(int x)\n    proves(x == x);\n");
    CPPL_CHECK(has_style_message(diagnostics, "one space before"));
}

CPPL_TEST(check_style_flags_missing_space_before_invariant_parenthesis) {
    const auto diagnostics =
        style_diagnostics("verified int f(int n)\n    ensures (result >= 0)\n{\n    while (n > 0)\n        invariant(n "
                          ">= 0)\n    {\n        --n;\n    }\n    return n;\n}\n");
    CPPL_CHECK(has_style_message(diagnostics, "one space before"));
}

CPPL_TEST(check_style_flags_missing_space_before_decreases_parenthesis) {
    const auto diagnostics = style_diagnostics("pure unsigned f(unsigned n)\n    decreases(n)\n{\n    return n;\n}\n");
    CPPL_CHECK(has_style_message(diagnostics, "one space before"));
}

CPPL_TEST(check_style_is_silent_on_canonical_law) {
    const auto diagnostics = style_diagnostics("law l(int x)\n    proves (x == x);\n");
    CPPL_CHECK(diagnostics.empty());
}

CPPL_TEST(check_style_is_silent_on_canonical_function_contract) {
    const auto diagnostics = style_diagnostics(
        "verified int f(int x)\n    expects (x >= 0)\n    ensures (result >= 0)\n{\n    return x;\n}\n");
    CPPL_CHECK(diagnostics.empty());
}

CPPL_TEST(check_style_is_silent_on_canonical_loop_clause) {
    const auto diagnostics =
        style_diagnostics("verified int f(int n)\n    ensures (result >= 0)\n{\n    while (n > 0)\n        invariant "
                          "(n >= 0)\n    {\n        --n;\n    }\n    return n;\n}\n");
    CPPL_CHECK(diagnostics.empty());
}

CPPL_TEST(check_style_does_not_flag_inline_refinement_where) {
    const auto diagnostics = style_diagnostics("type NonNegative = int where (self >= 0);\n");
    CPPL_CHECK(diagnostics.empty());
}

CPPL_TEST(check_style_ignores_clause_words_inside_comments_and_strings) {
    const auto diagnostics = style_diagnostics("// ensures(result) expects(x) proves(y) invariant(z) decreases(n)\n"
                                               "const char* s = \"ensures(result) proves(x)\";\n");
    CPPL_CHECK(diagnostics.empty());
}

CPPL_TEST(check_style_ignores_contextual_words_used_as_ordinary_identifiers) {
    const auto diagnostics =
        style_diagnostics("int expects = 0; int ensures = 1; int proves = 2; int invariant = 3;\n");
    CPPL_CHECK(diagnostics.empty());
}

CPPL_TEST(check_style_returns_only_style_category_diagnostics) {
    const auto diagnostics = style_diagnostics("law l(int x) proves(x==x);\n");
    CPPL_CHECK(!diagnostics.empty());
    for (const diagnostics::Diagnostic& diagnostic : diagnostics) {
        CPPL_CHECK(diagnostic.category == diagnostics::Category::Style);
    }
}

// -----------------------------------------------------------------------------
// Cross-feature stress and idempotency
// -----------------------------------------------------------------------------

CPPL_TEST(mixed_translation_unit_formats_all_cppl_regions_without_keyword_cross_talk) {
    const std::string input = "#include <cstddef>\n"
                              "int law=0;\n"
                              "type NonNegative=int where(self>=0);\n"
                              "law identity(unsigned x) proves(x==x);\n"
                              "verified pure unsigned f(unsigned x) expects(x>0u) ensures(result>0u) decreases(x){\n"
                              "while(x>1u) invariant(x>0u) decreases(x){--x;}\n"
                              "return x;\n"
                              "}\n"
                              "proof p() proves(1==1){refl;}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("int law = 0;") != std::string::npos);
    CPPL_CHECK(formatted.find("where (self >= 0)") != std::string::npos);
    CPPL_CHECK(formatted.find("\n    proves (x == x);") != std::string::npos);
    CPPL_CHECK(formatted.find("\n    expects (x > 0u)\n    ensures (result > 0u)\n    decreases (x)\n{") !=
               std::string::npos);
    CPPL_CHECK(formatted.find("\n        invariant (x > 0u)\n        decreases (x)\n    {") != std::string::npos);
    CPPL_CHECK(formatted.find("proof p()\n    proves (1 == 1)\n{") != std::string::npos);
}

CPPL_TEST(mixed_translation_unit_is_byte_stable_after_first_format) {
    const std::string input = "type N=int where(self>=0);\n"
                              "law l(int x) proves(x==x);\n"
                              "verified int f(int x) ensures(result==x){return x;}\n"
                              "proof p() proves(1==1){refl;}\n";
    const std::string once = format_text(input);
    const std::string twice = format_text(once);
    const std::string three_times = format_text(twice);
    CPPL_CHECK_EQ(once, twice);
    CPPL_CHECK_EQ(twice, three_times);
}

CPPL_TEST(formatting_does_not_change_number_of_cppl_clause_keywords) {
    const std::string input = "verified int f(int x) expects(x>=0) ensures(result>=0){\n"
                              "while(x>0) invariant(x>=0){--x;}\n"
                              "return x;\n"
                              "}\n"
                              "law l(int x) proves(x==x);\n";
    const std::string formatted = format_text(input);
    for (const std::string& keyword : {"expects", "ensures", "invariant", "proves"}) {
        CPPL_CHECK_EQ(count_occurrences(input, keyword), count_occurrences(formatted, keyword));
    }
}

CPPL_TEST(formatting_does_not_change_refinement_where_count) {
    const std::string input = "type A=int where(self>=0);\ntype B=A where(self<=10);\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK_EQ(count_occurrences(input, "where"), count_occurrences(formatted, "where"));
}

#ifdef CPPL_TEST_FIXTURES_DIR
CPPL_TEST(formatting_every_cppl_fixture_twice_is_idempotent) {
    const std::vector<std::string> fixtures = {
        "identity_law.cpp",   "verified_functions.cpp",  "verified_loops.cpp",      "refinement_types.cpp",
        "written_proof.cpp",  "true_arithmetic_law.cpp", "structural_cases.cpp",    "disjunction.cpp",
        "verified_calls.cpp", "verified_locals.cpp",     "verified_arithmetic.cpp", "verified_paths.cpp",
    };

    for (const std::string& fixture : fixtures) {
        const std::string path = std::string(CPPL_TEST_FIXTURES_DIR) + "/" + fixture;
        const std::string original = read_file(path);
        const std::string once = format_text(original);
        const std::string twice = format_text(once);
        if (once != twice) {
            fail_test("formatter is not idempotent for fixture '" + fixture + "'");
        }
    }
}

CPPL_TEST(formatting_every_cppl_fixture_returns_well_formed_original_byte_edits) {
    const std::vector<std::string> fixtures = {
        "identity_law.cpp",   "verified_functions.cpp",  "verified_loops.cpp",      "refinement_types.cpp",
        "written_proof.cpp",  "true_arithmetic_law.cpp", "structural_cases.cpp",    "disjunction.cpp",
        "verified_calls.cpp", "verified_locals.cpp",     "verified_arithmetic.cpp", "verified_paths.cpp",
    };

    for (const std::string& fixture : fixtures) {
        const std::string path = std::string(CPPL_TEST_FIXTURES_DIR) + "/" + fixture;
        const std::string original = read_file(path);
        const formatter::FormatResult result = document_result(original);
        if (!result.ok) {
            fail_test("formatter failed for fixture '" + fixture + "'");
        }
        check_edits_well_formed(original, result.edits);
    }
}
#endif
