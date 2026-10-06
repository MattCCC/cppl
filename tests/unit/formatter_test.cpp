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

// A directive inside a C++L declaration is part of the program: laying the
// declaration out again must neither drop it nor join it to another line.
CPPL_TEST(a_directive_inside_a_cppl_declaration_keeps_its_own_line) {
    const std::string input = "verified unsigned clamp(unsigned x)\n"
                              "#pragma pack(push, 1)\n"
                              "    expects (x < 10u)\n"
                              "#pragma pack(pop)\n"
                              "    ensures (result == x)\n"
                              "{\n"
                              "    return x;\n"
                              "}\n"
                              "\n"
                              "law bounded(unsigned x)\n"
                              "#if 1\n"
                              "    proves (x <= x)\n"
                              "#endif\n"
                              ";\n";
    const std::string output = format_text(input);
    for (const std::string directive :
         {"\n#pragma pack(push, 1)\n", "\n#pragma pack(pop)\n", "\n#if 1\n", "\n#endif\n"}) {
        CPPL_CHECK(output.find(directive) != std::string::npos);
    }
    CPPL_CHECK_EQ(count_occurrences(output, "#pragma"), 2U);
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
