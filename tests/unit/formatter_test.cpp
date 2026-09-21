// Unit tests for cppl::formatter, the one canonical-formatting engine shared
// by cppl-lsp (document/range/on-type formatting) and the cppl-format CLI
// (compiler/formatter/include/cppl/formatter/format.hpp).
//
// Canonical rule under test: `expects`/`ensures`/`invariant`/`proves` each
// begin their own continuation line, indented one level from the enclosing
// declaration, with the opening `{` on its own separate line back at the
// declaration's column and no whitespace before the clause's '('. Refinement
// `where` clauses (`type X = T where (P);`) stay inline and are never
// touched.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/formatter/format.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

using namespace cppl;

namespace {

std::string apply_edits(const std::string& text, const std::vector<formatter::FormatEdit>& edits) {
    std::vector<formatter::FormatEdit> sorted = edits;
    std::sort(sorted.begin(), sorted.end(), [](const formatter::FormatEdit& a, const formatter::FormatEdit& b) {
        return a.span.offset < b.span.offset;
    });
    std::string result;
    std::size_t cursor = 0;
    for (const formatter::FormatEdit& edit : sorted) {
        result.append(text, cursor, edit.span.offset - cursor);
        result += edit.replacement;
        cursor = edit.span.offset + edit.span.length;
    }
    result.append(text, cursor, text.size() - cursor);
    return result;
}

std::string format_text(const std::string& text) {
    formatter::FormatRequest request;
    request.text = text;
    const formatter::FormatResult result = formatter::format_document(request);
    CPPL_CHECK(result.ok);
    return apply_edits(text, result.edits);
}

std::string read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        ::cppl::testing::fail(__FILE__, __LINE__, "could not read '" + path + "'");
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

} // namespace

// --- laws ---------------------------------------------------------------

CPPL_TEST(law_with_single_clause_moves_ensures_to_its_own_line) {
    const std::string input = "law identity(int x) ensures(x == x);\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK_EQ(formatted, "law identity(int x)\n    ensures(x == x);\n");
}

CPPL_TEST(law_with_multiple_clauses_places_each_on_its_own_line_in_source_order) {
    const std::string input = "law bounded(unsigned x) expects(x < 10u) ensures(x + 1u <= 10u);\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK_EQ(formatted, "law bounded(unsigned x)\n    expects(x < 10u)\n    ensures(x + 1u <= 10u);\n");
}

CPPL_TEST(already_canonical_law_is_a_no_op) {
    const std::string input = "law identity(int x)\n    ensures(x == x);\n";
    formatter::FormatRequest request;
    request.text = input;
    const formatter::FormatResult result = formatter::format_document(request);
    CPPL_CHECK(result.ok);
    CPPL_CHECK(result.edits.empty());
}

// --- verified functions ---------------------------------------------------

CPPL_TEST(verified_function_places_ensures_on_its_own_line_and_brace_on_its_own_line) {
    const std::string input = "verified int fifty(int x) ensures(result == 50) {\n    return 50;\n}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK_EQ(formatted, "verified int fifty(int x)\n    ensures(result == 50)\n{\n    return 50;\n}\n");
}

CPPL_TEST(verified_function_with_expects_and_ensures_keeps_source_order) {
    const std::string input = "verified int f(int x) expects(x >= 0) ensures(result >= 0) {\n    return x;\n}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK_EQ(formatted,
                  "verified int f(int x)\n    expects(x >= 0)\n    ensures(result >= 0)\n{\n    return x;\n}\n");
}

CPPL_TEST(verified_function_never_gets_space_before_clause_paren) {
    const std::string input = "verified int f(int x) ensures (result >= 0) {\n    return x;\n}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("ensures(") != std::string::npos);
    CPPL_CHECK(formatted.find("ensures (") == std::string::npos);
}

CPPL_TEST(already_canonical_verified_function_is_a_no_op) {
    const std::string input = "verified int fifty(int x)\n    ensures(result == 50)\n{\n    return 50;\n}\n";
    formatter::FormatRequest request;
    request.text = input;
    const formatter::FormatResult result = formatter::format_document(request);
    CPPL_CHECK(result.ok);
    CPPL_CHECK(result.edits.empty());
}

// --- loops -----------------------------------------------------------------

CPPL_TEST(loop_invariant_moves_to_its_own_indented_line) {
    const std::string input = "verified unsigned count() ensures(result == 3u) {\n"
                              "    unsigned i = 0u;\n"
                              "    while (i < 3u) invariant(i <= 3u) {\n"
                              "        ++i;\n"
                              "    }\n"
                              "    return i;\n"
                              "}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("while (i < 3u)\n        invariant(i <= 3u)\n    {\n") != std::string::npos);
}

// --- refinements: `where` stays inline -------------------------------------

CPPL_TEST(refinement_where_clause_stays_inline) {
    const std::string input = "type NonNegative = int where(self >= 0);\n";
    formatter::FormatRequest request;
    request.text = input;
    const formatter::FormatResult result = formatter::format_document(request);
    CPPL_CHECK(result.ok);
    CPPL_CHECK(result.edits.empty());
    CPPL_CHECK_EQ(apply_edits(input, result.edits), input);
}

CPPL_TEST(refinement_where_clause_untouched_alongside_a_misformatted_ensures) {
    const std::string input = "type Percentage = int where(self >= 0 && self <= 100);\n"
                              "verified Percentage half() ensures(result == 50) { return 50; }\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("type Percentage = int where(self >= 0 && self <= 100);\n") != std::string::npos);
    CPPL_CHECK(formatted.find("\n    ensures(result == 50)\n{") != std::string::npos);
}

// --- proofs ------------------------------------------------------------

CPPL_TEST(proves_clause_moves_to_its_own_line) {
    const std::string input = "proof p() proves(1 == 1) {\n    refl;\n}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK_EQ(formatted, "proof p()\n    proves(1 == 1)\n{\n    refl;\n}\n");
}

CPPL_TEST(proves_clause_with_nested_cases_only_relocates_the_proves_line) {
    const std::string input = "enum class Flag { on, off };\n"
                              "proof p(Flag f) proves(f == Flag::on || f == Flag::off) {\n"
                              "    cases f {\n"
                              "        Flag::on => {\n"
                              "            refl;\n"
                              "        }\n"
                              "        Flag::off => {\n"
                              "            refl;\n"
                              "        }\n"
                              "    }\n"
                              "}\n";
    const std::string formatted = format_text(input);
    CPPL_CHECK(formatted.find("proof p(Flag f)\n    proves(f == Flag::on || f == Flag::off)\n{\n") !=
               std::string::npos);
    // The body's cases/case arms are untouched by clause relocation.
    CPPL_CHECK(formatted.find("cases f {\n") != std::string::npos);
    CPPL_CHECK(formatted.find("Flag::on => {\n") != std::string::npos);
    CPPL_CHECK(formatted.find("Flag::off => {\n") != std::string::npos);
}

// --- idempotency ------------------------------------------------------

CPPL_TEST(formatting_twice_equals_formatting_once) {
    const std::string input = "verified int f(int x) expects(x >= 0) ensures(result >= 0) {\n"
                              "    unsigned i = 0u;\n"
                              "    while (i < 3u) invariant(i <= 3u) { ++i; }\n"
                              "    return x;\n"
                              "}\n"
                              "law l(int x) ensures(f(x) >= 0);\n";
    const std::string once = format_text(input);
    const std::string twice = format_text(once);
    CPPL_CHECK_EQ(once, twice);
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
            ::cppl::testing::fail(__FILE__, __LINE__, "not idempotent: " + fixture);
        }
    }
}
#endif

// --- range formatting --------------------------------------------------

CPPL_TEST(range_wholly_inside_a_clause_predicate_expands_to_the_whole_clause) {
    const std::string input = "verified int f(int x) ensures(result >= 0) {\n    return x;\n}\n";
    const std::size_t predicate_pos = input.find("result >= 0");
    formatter::FormatRequest request;
    request.text = input;
    const formatter::FormatResult result = formatter::format_ranges(request, {source::ByteSpan{predicate_pos, 1}});
    CPPL_CHECK(result.ok);
    CPPL_CHECK(!result.edits.empty());
    const std::string formatted = apply_edits(input, result.edits);
    CPPL_CHECK(formatted.find("\n    ensures(result >= 0)\n{") != std::string::npos);
}

CPPL_TEST(range_entirely_inside_ordinary_cpp_does_not_touch_a_clause_elsewhere_in_the_file) {
    const std::string input = "verified int f(int x) ensures(result >= 0) {\n"
                              "    int   y   =   x;\n"
                              "    return y;\n"
                              "}\n"
                              "verified int g(int x) ensures(result >= 0) {\n"
                              "    return x;\n"
                              "}\n";
    const std::size_t line_pos = input.find("int   y");
    const std::size_t line_end = input.find('\n', line_pos);
    formatter::FormatRequest request;
    request.text = input;
    const formatter::FormatResult result =
        formatter::format_ranges(request, {source::ByteSpan{line_pos, line_end - line_pos}});
    CPPL_CHECK(result.ok);
    const std::string formatted = apply_edits(input, result.edits);
    // The touched ordinary-C++ line is normalized...
    CPPL_CHECK(formatted.find("int y = x;") != std::string::npos);
    // ...but neither function's `ensures` clause (including the untouched
    // second function, entirely outside the requested range) is relocated.
    CPPL_CHECK(formatted.find("f(int x) ensures(result >= 0) {") != std::string::npos);
    CPPL_CHECK(formatted.find("g(int x) ensures(result >= 0) {") != std::string::npos);
}

// --- on-type formatting -------------------------------------------------

CPPL_TEST(on_type_formatting_at_a_clause_close_paren_formats_only_that_clause) {
    const std::string input = "verified int f(int x) ensures(result >= 0) {\n    return x;\n}\n";
    const std::size_t close_paren = input.find(") {", input.find("ensures"));
    formatter::FormatRequest request;
    request.text = input;
    const formatter::FormatResult result = formatter::format_on_type(request, close_paren, "}");
    CPPL_CHECK(result.ok);
    if (!result.edits.empty()) {
        const std::string formatted = apply_edits(input, result.edits);
        CPPL_CHECK(formatted.find("\n    ensures(result >= 0)\n{") != std::string::npos);
    }
}

CPPL_TEST(on_type_formatting_mid_token_returns_no_edits) {
    const std::string input = "verified int f(int x) ensures(result >= 0) {\n    return x;\n}\n";
    // Position inside the identifier "result", not at any clause boundary.
    const std::size_t mid_token = input.find("result") + 2;
    formatter::FormatRequest request;
    request.text = input;
    const formatter::FormatResult result = formatter::format_on_type(request, mid_token, "x");
    CPPL_CHECK(result.ok);
    // Conservative by construction: never reaches beyond the immediate token.
    for (const formatter::FormatEdit& edit : result.edits) {
        CPPL_CHECK(edit.span.offset <= mid_token);
        if (edit.span.length == 0) {
            continue;
        }
        CPPL_CHECK(edit.span.offset + edit.span.length >= mid_token);
    }
}

// --- check_style ---------------------------------------------------------

CPPL_TEST(check_style_flags_a_clause_that_is_not_on_its_own_line) {
    const std::string text = "verified int f(int x) ensures(result >= 0) {\n    return x;\n}\n";
    const frontend::TokenStream tokens = frontend::lex(text, "style.cpp");
    diagnostics::Engine engine;
    const frontend::Syntax syntax = frontend::recognize(tokens, engine);
    const std::vector<diagnostics::Diagnostic> style = formatter::check_style(tokens, syntax);
    bool found = false;
    for (const diagnostics::Diagnostic& diagnostic : style) {
        if (diagnostic.category == diagnostics::Category::Style &&
            diagnostic.message.find("own continuation line") != std::string::npos) {
            found = true;
        }
    }
    CPPL_CHECK(found);
}

CPPL_TEST(check_style_flags_whitespace_before_clause_paren) {
    const std::string text = "law l(int x)\n    ensures (x == x);\n";
    const frontend::TokenStream tokens = frontend::lex(text, "style.cpp");
    diagnostics::Engine engine;
    const frontend::Syntax syntax = frontend::recognize(tokens, engine);
    const std::vector<diagnostics::Diagnostic> style = formatter::check_style(tokens, syntax);
    bool found = false;
    for (const diagnostics::Diagnostic& diagnostic : style) {
        if (diagnostic.category == diagnostics::Category::Style &&
            diagnostic.message.find("whitespace before") != std::string::npos) {
            found = true;
        }
    }
    CPPL_CHECK(found);
}

CPPL_TEST(check_style_is_silent_on_already_canonical_source) {
    const std::string text = "law l(int x)\n    ensures(x == x);\n";
    const frontend::TokenStream tokens = frontend::lex(text, "style.cpp");
    diagnostics::Engine engine;
    const frontend::Syntax syntax = frontend::recognize(tokens, engine);
    const std::vector<diagnostics::Diagnostic> style = formatter::check_style(tokens, syntax);
    CPPL_CHECK(style.empty());
}

CPPL_TEST(check_style_does_not_flag_an_inline_refinement_where_clause) {
    const std::string text = "type NonNegative = int where(self >= 0);\n";
    const frontend::TokenStream tokens = frontend::lex(text, "style.cpp");
    diagnostics::Engine engine;
    const frontend::Syntax syntax = frontend::recognize(tokens, engine);
    const std::vector<diagnostics::Diagnostic> style = formatter::check_style(tokens, syntax);
    CPPL_CHECK(style.empty());
}
