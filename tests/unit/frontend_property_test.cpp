// Deterministic malformed-source campaigns through the real lexer, recognizer,
// and projector. Successful recognition is not a verification result.
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/erasure/erase.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {
void exercise(const std::string& input) {
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(input, "fuzz.cpp");
    std::size_t end = 0;
    for (const auto& token : stream.tokens()) {
        CPPL_CHECK(token.span.offset >= end);
        CPPL_CHECK(token.span.offset <= input.size());
        CPPL_CHECK(token.span.length <= input.size() - token.span.offset);
        end = token.span.end();
    }
    CPPL_CHECK(stream.tokens().back().kind == cppl::frontend::TokenKind::EndOfFile);
    CPPL_CHECK_EQ(end, input.size());
    const auto syntax = cppl::frontend::recognize(stream, engine);
    if (engine.has_errors())
        return; // the driver likewise stops on syntax errors
    const auto projection = cppl::frontend::project(stream, syntax, {});
    const auto repeat = cppl::frontend::project(stream, syntax, {});
    CPPL_CHECK_EQ(projection.analysis, repeat.analysis);
    CPPL_CHECK_EQ(projection.runtime, repeat.runtime);
    CPPL_CHECK_EQ(projection.runtime.size(), input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\n')
            CPPL_CHECK_EQ(projection.runtime[i], '\n');
        if (input[i] != projection.runtime[i])
            CPPL_CHECK_EQ(projection.runtime[i], ' ');
    }
    const auto erased = cppl::erasure::erase(stream, syntax, projection, engine);
    CPPL_CHECK(erased.report.only_deletions);
    CPPL_CHECK(erased.report.lines_preserved);
    CPPL_CHECK(!engine.has_errors());
    for (const auto& declaration : projection.declaration_offsets) {
        CPPL_CHECK(declaration.original < input.size());
        CPPL_CHECK(declaration.analysis < projection.analysis.size());
        CPPL_CHECK_EQ(input[declaration.original], projection.analysis[declaration.analysis]);
    }
}

const std::vector<std::string> seeds{
    "law l(unsigned x) expects (x == 1u) proves (x == 1u);",
    "proof p(unsigned x) proves (l(x)) { assume h : x == 1u; rewrite h; refl; }",
    "proof q() proves (l(1u)) { apply p(1u); exact p(1u); }",
    "verified unsigned f(unsigned x) ensures (result == x) { unsigned y = x; return y; }",
    "verified pure unsigned f() ensures (result == 0u) {return 0u;}law l() proves (f() == 0u);",
    "namespace N { pure unsigned f(unsigned x) {return x;} law l() proves (f(0u) == 0u); }",
    "struct law {}; law f(); int proof = 1; int verified = 2; int pure = 3;",
    "const char* s = R\"tag(law false() proves (0 == 1); proof p() { })tag\";",
    "// law false() proves (0 == 1);\n/* proof p() proves (q()) {refl;} */",
    "# 19 \"header.hpp\"\n  law l()\n proves (0u == 0u);",
    "#line 4294967295 \"same.cpp\"\r\nlaw l() ensures (0u == 0u);\r\n",
};
} // namespace

CPPL_TEST(every_seed_prefix_and_single_byte_deletion_is_safe) {
    for (const auto& seed : seeds) {
        for (std::size_t size = 0; size <= seed.size(); ++size) {
            exercise(seed.substr(0, size));
            if (size < seed.size()) {
                auto deleted = seed;
                deleted.erase(size, 1);
                exercise(deleted);
            }
        }
    }
}

CPPL_TEST(deterministic_byte_mutations_preserve_frontend_boundaries) {
    std::uint64_t state = 0x71b8906ce14d352aULL;
    const auto next = [&]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    const std::string bytes = std::string("{}();:\"'/*\\\r\n\t#,_0a") + '\0' + '\xff';
    for (unsigned sample = 0; sample < 4000; ++sample) {
        auto input = seeds[next() % seeds.size()];
        for (unsigned mutation = 0; mutation < 1 + sample % 5; ++mutation) {
            const auto at = static_cast<std::size_t>(next() % (input.size() + 1));
            input.insert(at, 1, bytes[next() % bytes.size()]);
        }
        exercise(input);
    }
}

CPPL_TEST(long_tokens_and_deep_or_incomplete_source_do_not_crash) {
    const std::string name(32768, 'x');
    exercise("law " + name + "() ensures (0u == 0u);");
    exercise("proof " + name + "() proves (l()) { refl; }");
    exercise("law l() proves (" + std::string(2048, '(') + "0u == 0u" + std::string(2048, ')') + ");");
    exercise("proof p() proves (l()) { exact q(" + std::string(2048, '('));
    std::string repeated;
    for (unsigned i = 0; i < 300; ++i)
        repeated += "#line 1 \"same.cpp\"\nlaw l() ensures (0u == 0u);\n";
    exercise(repeated);
}
