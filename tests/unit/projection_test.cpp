// Projection and erasure properties.
//
// The runtime program must be the scanned program with C++L-only spans removed
// and nothing else changed. That property is what makes a C++17 target stay
// C++17: erasure can only delete, so it cannot introduce a construct from a
// later standard (COMPATIBILITY.md, TRUST.md 7).

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/erasure/erase.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <string>

namespace {

const std::string kUnit =
    "# 1 \"main.cpp\"\n"
    "pure int identity(int x) {\n"
    "    return x;\n"
    "}\n"
    "law identity_returns_input(int x)\n"
    "    ensures(identity(x) == x);\n"
    "int main() { return identity(0); }\n";

std::size_t count_newlines(std::string_view text) {
    return static_cast<std::size_t>(std::ranges::count(text, '\n'));
}

}  // namespace

CPPL_TEST(the_runtime_program_only_loses_text) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(projection.runtime.size(), kUnit.size());
    CPPL_CHECK_EQ(count_newlines(projection.runtime), count_newlines(kUnit));

    for (std::size_t offset = 0; offset < kUnit.size(); ++offset) {
        if (projection.runtime[offset] != kUnit[offset]) {
            CPPL_CHECK_EQ(projection.runtime[offset], ' ');
        }
    }
}

CPPL_TEST(every_runtime_token_is_an_original_token_in_its_original_place) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    const cppl::frontend::TokenStream runtime =
        cppl::frontend::lex(projection.runtime, "main.cpp");

    std::size_t original_index = 0;
    for (const cppl::frontend::Token& token : runtime.tokens()) {
        if (token.kind == cppl::frontend::TokenKind::EndOfFile) {
            continue;
        }
        bool matched = false;
        while (original_index < stream.tokens().size()) {
            const cppl::frontend::Token& candidate = stream.tokens()[original_index++];
            if (candidate.span.offset == token.span.offset && candidate.text == token.text &&
                candidate.line == token.line && candidate.column == token.column) {
                matched = true;
                break;
            }
        }
        CPPL_CHECK(matched);
    }
}

CPPL_TEST(the_runtime_program_carries_no_formal_syntax) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    const cppl::frontend::TokenStream runtime =
        cppl::frontend::lex(projection.runtime, "main.cpp");
    for (const cppl::frontend::Token& token : runtime.tokens()) {
        CPPL_CHECK(token.text != "law");
        CPPL_CHECK(token.text != "ensures");
        CPPL_CHECK(token.text != "pure");
    }
}

CPPL_TEST(the_analysis_program_carries_the_proposition_for_clang_to_resolve) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    CPPL_CHECK_EQ(projection.specification_functions.size(), std::size_t{1});
    const std::string& name = projection.specification_functions[0].name;
    CPPL_CHECK(projection.analysis.find(name) != std::string::npos);
    CPPL_CHECK(projection.analysis.find("identity(x) == x") != std::string::npos);
    CPPL_CHECK(projection.analysis.find("#line") != std::string::npos);
}

CPPL_TEST(erasure_reports_the_properties_it_checked) {
    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(kUnit, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);
    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});

    const cppl::erasure::Erased erased =
        cppl::erasure::erase(stream, syntax, projection, engine);

    CPPL_CHECK(erased.report.only_deletions);
    CPPL_CHECK(erased.report.lines_preserved);
    CPPL_CHECK(erased.report.erased_spans == 2);
    CPPL_CHECK(erased.report.erased_bytes > 0);
    CPPL_CHECK(!engine.has_errors());
}

CPPL_TEST(a_unit_without_formal_syntax_is_left_untouched) {
    const std::string ordinary = "int main() { return 0; }\n";

    cppl::diagnostics::Engine engine;
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(ordinary, "main.cpp");
    const cppl::frontend::Syntax syntax = cppl::frontend::recognize(stream, engine);

    CPPL_CHECK(syntax.empty());

    const cppl::frontend::Projection projection =
        cppl::frontend::project(stream, syntax, cppl::frontend::ProjectionOptions{});
    CPPL_CHECK_EQ(projection.runtime, ordinary);
    CPPL_CHECK_EQ(projection.analysis, ordinary);
}
