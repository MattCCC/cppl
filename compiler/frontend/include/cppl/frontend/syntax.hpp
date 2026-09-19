#pragma once

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::frontend {

enum class ClauseKind : std::uint8_t {
    Ensures,
    Expects,
    Invariant,
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
    source::SourceRange range; // the whole declaration, including its ';'
    source::SourceLocation keyword_location;
    std::uint32_t end_line = 0; // presumed line of the terminating ';'
    source::ByteSpan parameters;
    std::vector<Clause> clauses;

    [[nodiscard]] const Clause* proposition() const;

    // The precondition the proposition is stated under, if the law has one.
    [[nodiscard]] const Clause* premise() const;
};

// The primitive proof statements of GRAMMAR.md 5.1 - 5.5.
enum class ProofStatementKind : std::uint8_t {
    Reflexivity,
    Exact,
    Apply,
    Assume,
    Rewrite,
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
    source::SourceRange range; // the whole declaration, including its body
    source::SourceLocation keyword_location;
    std::uint32_t end_line = 0; // presumed line of the closing '}'
    source::ByteSpan parameters;
    source::ByteSpan proposition;
    source::SourceLocation proposition_location;
    std::vector<ProofStatement> statements;
};

// verified [pure] T f(params) [expects(P)] ensures(Q) { body }  (GRAMMAR.md 6)
//
// The contract is carried here as spans. What it means is decided once Clang
// has resolved it, like every other specification expression.
struct VerifiedFunction {
    source::ByteSpan keyword; // the `verified` specifier itself
    source::SourceLocation keyword_location;
    std::string function_name;
    source::SourceLocation function_location;
    std::size_t function_offset = 0; // physical byte offset in the preprocessed input

    source::ByteSpan return_type;
    source::ByteSpan parameters;

    // The clauses, and the region of text they occupy between the parameter
    // list and the body. The region is removed from both projections: a
    // contract is not C++.
    std::vector<Clause> clauses;
    source::ByteSpan clause_region;

    // Where the generated contract functions are emitted: after the body, so
    // that everything the contract can name is already declared.
    std::size_t body_end = 0;
    std::uint32_t body_end_line = 0;
    std::uint32_t body_end_column = 0;

    [[nodiscard]] const Clause* postcondition() const;
    [[nodiscard]] const Clause* precondition() const;
};

// while (condition) invariant(P)... { body }
// for (init; condition; increment) invariant(P)... { body }   (GRAMMAR.md 25, 26)
//
// The clauses stand between the loop header and its body. They are not C++, so
// they leave both texts; Clang is instead given one declaration per invariant
// at the start of the body, where every name the invariant may use is in scope
// and nothing the body declares is yet.
struct LoopSpecification {
    std::size_t function_index = 0; // the verified function whose body holds the loop
    source::SourceLocation keyword_location;
    std::vector<Clause> invariants;
    std::vector<source::SourceLocation> expression_locations; // one per invariant
    source::ByteSpan clause_region;

    // Just past the body's '{', where the invariant declarations are inserted,
    // and the presumed position the text after it resumes at.
    std::size_t body_open = 0;
    std::uint32_t body_open_line = 0;
    std::uint32_t body_open_column = 0;
};

// The `pure` declaration specifier and the function it applies to (SPEC.md 13).
struct PureMarker {
    source::ByteSpan keyword;
    source::SourceLocation keyword_location;
    std::string function_name;
    source::SourceLocation function_location;
    std::size_t function_offset = 0;
};

struct Syntax {
    std::vector<LawDeclaration> laws;
    std::vector<ProofDeclaration> proofs;
    std::vector<PureMarker> pure_markers;
    std::vector<VerifiedFunction> verified_functions;
    std::vector<LoopSpecification> loops;

    [[nodiscard]] bool empty() const noexcept {
        return laws.empty() && proofs.empty() && pure_markers.empty() && verified_functions.empty() && loops.empty();
    }
};

// Recognizes C++L constructs in a preprocessed token stream.
//
// Recognition is contextual: a C++L word is only a C++L word where the
// complete grammatical context makes the ordinary C++ reading impossible
// (SPEC.md 3, 3.1). Ordinary declarations such as `int law = 1;` are left
// untouched.
[[nodiscard]] Syntax recognize(const TokenStream& tokens, diagnostics::Engine& diagnostics);

} // namespace cppl::frontend
