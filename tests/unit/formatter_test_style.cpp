// Formatter tests (formatter_test): style diagnostics, and stress and
// idempotency across features and over the fixture corpus.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/formatter/format.hpp"
#include "cppl/testing/test.hpp"
#include "formatter_test_support.hpp"

#include <string>
#include <vector>

using namespace formatter_test_detail;

using namespace cppl;

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
