// Formatter tests (formatter_test): range and on-type formatting.

#include "cppl/formatter/format.hpp"
#include "cppl/source/location.hpp"
#include "cppl/testing/test.hpp"
#include "formatter_test_support.hpp"

#include <cstddef>
#include <string>
#include <vector>

using namespace formatter_test_detail;

using namespace cppl;

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
