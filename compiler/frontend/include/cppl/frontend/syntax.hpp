#pragma once

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::frontend {

enum class ClauseKind : std::uint8_t {
    Ensures,
    Expects,
    Invariant,
    Proves,
    Decreases,
};

std::string describe(ClauseKind kind);

struct Clause {
    ClauseKind kind = ClauseKind::Ensures;
    source::ByteSpan keyword; // the clause keyword itself, e.g. 'ensures'
    source::ByteSpan expression;
    source::SourceLocation location;
};

// law name(parameters) proves (proposition);   (SPEC.md 10.1, GRAMMAR.md 3)
struct LawDeclaration {
    std::string name;
    source::SourceRange range; // the whole declaration, including its ';'
    source::SourceLocation keyword_location;
    std::uint32_t end_line = 0; // presumed line of the terminating ';'
    source::ByteSpan parameters;
    std::vector<Clause> clauses;

    // Whether the author wrote `trusted law` (SPEC.md 27): the proposition is
    // assumed, not proved, and is reported as an explicit trusted assumption.
    bool trusted = false;

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
    Cases,
    Decompose,
};

std::string describe(ProofStatementKind kind);

// One term a referenced proof is instantiated at. The span is ordinary C++ and
// is never read here: it is handed to Clang through the projection, like every
// other expression in the language (SPEC.md 7.3).
struct ProofArgument {
    source::ByteSpan span;
    source::SourceLocation location;
};

struct ProofArm;

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
    std::vector<ProofArm> arms;
};

// One arm of a `cases` statement (GRAMMAR.md 5.6).
//
// The recognizer reads arm syntax without knowing what the subject is. Which
// case a label denotes, how many binders the case supplies, and whether the
// arms are exhaustive are all settled later against the subject's decomposition
// provider (SPEC.md 20.5).
struct ProofArm {
    // The label's source span, kept so diagnostics and editors point at what
    // was written, and its spelling, which is what a reserved label is matched
    // against.
    source::ByteSpan label;
    std::string spelling;
    source::SourceLocation location;
    // The label is a name a representation reserves for a state that has no C++
    // expression, rather than an expression for Clang to resolve.
    bool keyword_label = false;
    std::vector<std::string> binders;
    std::vector<ProofStatement> statements;
};

// proof name(parameters) proves(proposition) { statements }   (GRAMMAR.md 4)
struct ProofDeclaration {
    // An inline Law body uses the same proof pipeline and owns only its body
    // span. The Law owns the preceding header; erasure covers both spans.
    std::optional<std::size_t> inline_law;
    std::string name;
    source::SourceRange range; // the whole declaration, including its body
    source::SourceLocation keyword_location;
    std::uint32_t end_line = 0; // presumed line of the closing '}'
    source::ByteSpan parameters;
    source::ByteSpan proves_keyword; // the 'proves' token itself
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
    [[nodiscard]] std::vector<const Clause*> preconditions() const; // in source order
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
    source::ByteSpan keyword;       // the 'while' or 'for' token itself
    source::SourceLocation keyword_location;
    std::vector<Clause> invariants;
    std::optional<Clause> decreases;
    std::vector<source::SourceLocation> expression_locations; // one per invariant
    source::ByteSpan clause_region;

    // Just past the body's '{', where the invariant declarations are inserted,
    // and the presumed position the text after it resumes at.
    std::size_t body_open = 0;
    std::uint32_t body_open_line = 0;
    std::uint32_t body_open_column = 0;
};

// type name [(index parameters)] = base-type where (predicate);
//                                            (SPEC.md 17, 18; GRAMMAR.md 14, 16)
//
// A refinement type is a verification-level type over an ordinary C++ base type:
// `{ self : T | P(self) }`. It has no runtime representation of its own, so this
// declaration is runtime-bearing rather than proof-only: it lowers to the alias
// `using name = base;` in the program, and only the predicate leaves it. The
// predicate is resolved through the analysis projection with `self` bound to a
// value of the base type, like every other specification expression.
struct RefinementType {
    std::string name;
    source::SourceRange range; // the whole declaration, including its ';'
    source::SourceLocation keyword_location;
    std::uint32_t end_line = 0; // presumed line of the terminating ';'

    // The index parameter list, empty when the declaration has none. Indices are
    // verification-level; they never reach the alias.
    source::ByteSpan indices;
    bool indexed = false;

    source::ByteSpan base; // the base type-id, an ordinary C++ type
    source::SourceLocation base_location;

    source::ByteSpan predicate; // the expression inside `where ( ... )`
    source::SourceLocation predicate_location;
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
    std::vector<RefinementType> refinement_types;

    [[nodiscard]] bool empty() const noexcept {
        return laws.empty() && proofs.empty() && pure_markers.empty() && verified_functions.empty() && loops.empty() &&
               refinement_types.empty();
    }
};

// Recognizes C++L constructs in a preprocessed token stream.
//
// Recognition is contextual: a C++L word is only a C++L word where the
// complete grammatical context makes the ordinary C++ reading impossible
// (SPEC.md 3, 3.1). Ordinary declarations such as `int law = 1;` are left
// untouched.
enum class RecognitionMode : std::uint8_t { Compile, Edit };

[[nodiscard]] Syntax recognize(const TokenStream& stream, diagnostics::Engine& engine,
                               RecognitionMode mode = RecognitionMode::Compile);

} // namespace cppl::frontend
