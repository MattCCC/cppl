#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"

namespace cppl::frontend {

enum class ClauseKind : std::uint8_t {
    Ensures,
    Expects,
};

std::string describe(ClauseKind kind);

struct Clause {
    ClauseKind kind = ClauseKind::Ensures;
    source::ByteSpan expression;
    source::SourceLocation location;
};

// law name(parameters) ensures(proposition);   (SPEC.md 10.1, GRAMMAR.md 3)
struct LawDeclaration {
    std::string name;
    source::SourceRange range;  // the whole declaration, including its ';'
    source::SourceLocation keyword_location;
    std::uint32_t end_line = 0;  // presumed line of the terminating ';'
    source::ByteSpan parameters;
    std::vector<Clause> clauses;

    [[nodiscard]] const Clause* proposition() const;
};

// The `pure` declaration specifier and the function it applies to (SPEC.md 13).
struct PureMarker {
    source::ByteSpan keyword;
    source::SourceLocation keyword_location;
    std::string function_name;
    source::SourceLocation function_location;
};

struct Syntax {
    std::vector<LawDeclaration> laws;
    std::vector<PureMarker> pure_markers;

    [[nodiscard]] bool empty() const noexcept {
        return laws.empty() && pure_markers.empty();
    }
};

// Recognizes C++L constructs in a preprocessed token stream.
//
// Recognition is contextual: a C++L word is only a C++L word where the
// complete grammatical context makes the ordinary C++ reading impossible
// (SPEC.md 3, 3.1). Ordinary declarations such as `int law = 1;` are left
// untouched.
[[nodiscard]] Syntax recognize(const TokenStream& tokens, diagnostics::Engine& diagnostics);

}  // namespace cppl::frontend
