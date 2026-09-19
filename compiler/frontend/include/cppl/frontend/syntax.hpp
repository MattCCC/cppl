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

    // The precondition the proposition is stated under, if the law has one.
    [[nodiscard]] const Clause* premise() const;
};

// The primitive proof statements of GRAMMAR.md 5.1 - 5.4.
enum class ProofStatementKind : std::uint8_t {
    Reflexivity,
    Exact,
    Apply,
    Assume,
};

std::string describe(ProofStatementKind kind);

// One term a referenced proof is instantiated at. The span is ordinary C++ and
// is never read here: it is handed to Clang through the projection, like every
// other expression in the language (SPEC.md 7.3).
struct ProofArgument {
    source::ByteSpan span;
    source::SourceLocation location;
};

struct ProofStatement {
    ProofStatementKind kind = ProofStatementKind::Reflexivity;

    // The proof named by `exact` or `apply`, or the name `assume` binds.
    std::string reference;
    std::vector<ProofArgument> arguments;

    // The proposition written after `assume h :`. Ordinary C++, delimited here
    // and resolved by Clang through the projection, like every other expression.
    source::ByteSpan proposition;
    source::SourceLocation proposition_location;

    source::SourceLocation location;
};

// proof name(parameters) proves(proposition) { statements }   (GRAMMAR.md 4)
struct ProofDeclaration {
    std::string name;
    source::SourceRange range;  // the whole declaration, including its body
    source::SourceLocation keyword_location;
    std::uint32_t end_line = 0;  // presumed line of the closing '}'
    source::ByteSpan parameters;
    source::ByteSpan proposition;
    source::SourceLocation proposition_location;
    std::vector<ProofStatement> statements;
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
    std::vector<ProofDeclaration> proofs;
    std::vector<PureMarker> pure_markers;

    [[nodiscard]] bool empty() const noexcept {
        return laws.empty() && proofs.empty() && pure_markers.empty();
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
