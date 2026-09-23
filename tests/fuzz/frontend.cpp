// The C++L frontend over arbitrary source text: lexing, recognition of the
// C++L constructs, projection to analysis and runtime C++, and erasure. Any
// file a user compiles or opens in the editor reaches it.
//
// Properties: the invariants tests/unit/frontend_property_test.cpp states for
// its deterministic campaign, over inputs no one chose. Tokens tile the input
// in order; projection is deterministic, keeps every newline and only blanks
// characters; erasure only deletes and keeps every line.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/erasure/erase.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/testing/fuzz.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using cppl::testing::fuzz::require;

    const std::string input = cppl::testing::fuzz::text({data, size});
    cppl::diagnostics::Engine engine;
    const auto stream = cppl::frontend::lex(input, "fuzz.cpp");

    std::size_t end = 0;
    for (const auto& token : stream.tokens()) {
        require(token.span.offset >= end, "tokens are in order and do not overlap");
        require(token.span.offset <= input.size(), "a token starts inside the input");
        require(token.span.length <= input.size() - token.span.offset, "a token ends inside the input");
        end = token.span.end();
    }
    require(!stream.tokens().empty() && stream.tokens().back().kind == cppl::frontend::TokenKind::EndOfFile,
            "the token stream ends with EndOfFile");
    require(end == input.size(), "the tokens reach the end of the input");

    const auto syntax = cppl::frontend::recognize(stream, engine);
    if (engine.has_errors()) {
        return 0; // the driver likewise stops on syntax errors
    }

    const auto projection = cppl::frontend::project(stream, syntax, {});
    const auto repeat = cppl::frontend::project(stream, syntax, {});
    require(projection.analysis == repeat.analysis && projection.runtime == repeat.runtime,
            "projection is deterministic");
    require(projection.runtime.size() == input.size(), "the runtime projection keeps every byte position");
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\n') {
            require(projection.runtime[i] == '\n', "the runtime projection keeps every newline");
        }
        if (input[i] != projection.runtime[i]) {
            require(projection.runtime[i] == ' ', "the runtime projection only blanks characters");
        }
    }
    for (const auto& declaration : projection.declaration_offsets) {
        require(declaration.original < input.size(), "a declaration offset is inside the input");
        require(declaration.analysis < projection.analysis.size(), "a declaration offset is inside the analysis");
        require(input[declaration.original] == projection.analysis[declaration.analysis],
                "a declaration offset maps a character to itself");
    }

    const auto erased = cppl::erasure::erase(stream, syntax, projection, engine);
    require(erased.report.only_deletions, "erasure only deletes");
    require(erased.report.lines_preserved, "erasure keeps every line");
    require(!engine.has_errors(), "erasure of recognized source reports no error");
    return 0;
}
