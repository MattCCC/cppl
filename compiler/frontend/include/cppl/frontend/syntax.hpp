#pragma once

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"

#include <cstddef>
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
    // Everything between the parentheses: the `)` closing it is the byte at
    // `expression.end()`.
    source::ByteSpan expression;
    source::SourceLocation location;
};

// One component of a `decreases` measure. `decreases (outer, inner)` is one
// lexicographic list whose components its top-level commas separate; a single
// measure is a list of one (SPEC.md TERMINATION-004).
struct MeasureComponent {
    source::ByteSpan expression;
    source::SourceLocation location; // where the component's first token stands
};

// The components of `clause`, a `decreases` clause read from `stream`, in the
// order written. A component with no tokens is returned empty, for the caller
// to refuse.
[[nodiscard]] std::vector<MeasureComponent> measure_components(const TokenStream& stream, const Clause& clause);

// What a clause is written on.
enum class ClauseOwner : std::uint8_t {
    Law,
    Proof,
    VerifiedFunction,
};

// The clauses that may be written at byte `at` of a declaration of `owner`
// that already has `written`, in the order the grammar has them: each kind at
// most once, and `expects` before the conclusion (GRAMMAR.md 3, 4, 6), exactly
// as the recognizer checks a declaration's clauses. A Law takes `expects` and
// `proves`, a proof `proves`, a verified function `expects` and `ensures`:
// `decreases` is refused there, since termination is not verified by this
// implementation.
[[nodiscard]] std::vector<ClauseKind> clauses_admitted(ClauseOwner owner, const std::vector<Clause>& written,
                                                       std::size_t at);

// How much of a Law or a proof has been written.
//
// A compiled unit only ever holds whole declarations. A draft
// (RecognitionMode::Draft) also holds one still being written, so that an
// editor reads where the author is from the recognizer rather than from the
// tokens: the recognizer is the one authority for what C++L is.
enum class Completeness : std::uint8_t {
    Whole,
    // `law name(parameters)` or `proof name(parameters)` and no clause after
    // it yet. It is ordinary C++ until a clause is written (SPEC.md 3.1); a
    // draft records it because a clause may be written there.
    AwaitingClause,
    // Its clauses, and neither a Law's `;` nor a body after them yet.
    AwaitingBody,
    // A body whose closing `}` is not written: it runs to the end of the text.
    UnterminatedBody,
};

// law name(parameters) proves (proposition);   (SPEC.md 10.1, GRAMMAR.md 3)
struct LawDeclaration {
    std::string name;
    source::SourceLocation name_location;
    source::ByteSpan name_span; // the name as written
    source::SourceRange range;  // the whole declaration, including its ';'
    Completeness completeness = Completeness::Whole;
    source::SourceLocation keyword_location;
    source::ByteSpan keyword;         // `law` itself
    source::ByteSpan trusted_keyword; // `trusted`, where it is written
    std::uint32_t end_line = 0;       // presumed line of the terminating ';'
    source::ByteSpan parameters;
    std::vector<Clause> clauses;

    // Whether the author wrote `trusted law` (SPEC.md 27): the proposition is
    // assumed, not proved, and is reported as an explicit trusted assumption.
    bool trusted = false;

    [[nodiscard]] const Clause* proposition() const;

    // The precondition the proposition is stated under, if the law has one.
    [[nodiscard]] const Clause* premise() const;
};

// The primitive proof statements of GRAMMAR.md 5.1 - 5.8.
enum class ProofStatementKind : std::uint8_t {
    Reflexivity,
    Exact,
    Apply,
    Assume,
    Rewrite,
    // "contradiction" evidence-reference ";" (GRAMMAR.md 5.6). Closes the goal
    // from evidence that the context cannot occur (SPEC.md CASE-011). Written
    // after `omit label by`, it is how a case is accounted for without an arm
    // (CASE-004). The evidence is checked like any other: this is a spelling for
    // existing rules, not a new one.
    Contradiction,
    Cases,
    Decompose,
    // "induction" identifier ";" | "induction" identifier "{" proof-arm... "}"
    // (GRAMMAR.md 5.8). Recognized at the syntax level like `Cases`/
    // `Decompose` so the formatter can lay out its arms; this implementation's
    // formal core has no induction rule (SPEC.md 21), so elaboration
    // rejects it exactly as it rejected the previously-unparsed spelling -
    // structural recognition here must never be read as semantic support.
    Induction,
    // A statement the recognizer could not read, up to the `;` that ends it or
    // the `}` that closes a block it opened. Only a draft
    // (RecognitionMode::Draft) records one, so that what follows it is read
    // still; a compiled unit refuses the proof instead.
    Unread,
};

// The word a statement of `kind` begins with (`refl`, `exact`, ...).
std::string describe(ProofStatementKind kind);

// The statement a word begins, where it begins one: the inverse of `describe`.
[[nodiscard]] std::optional<ProofStatementKind> statement_keyword(std::string_view word);

// Whether a statement of `kind` names evidence after its keyword: `exact`,
// `apply`, `rewrite` and `contradiction` (GRAMMAR.md 5.2, 5.3, 5.5, 5.6).
[[nodiscard]] bool names_evidence(ProofStatementKind kind);

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

    // The proof named by `exact` or `apply`, or the name `assume` binds, and
    // where that name is written.
    std::string reference;
    source::SourceLocation reference_location;
    source::ByteSpan reference_span;
    std::vector<ProofArgument> arguments;

    // The proposition written after `assume h :`. Ordinary C++, delimited here
    // and resolved by Clang through the projection, like every other expression.
    source::ByteSpan proposition;
    source::SourceLocation proposition_location;

    source::ByteSpan keyword; // the statement's keyword itself, e.g. 'contradiction'
    source::SourceLocation location;
    // The whole statement: its keyword through its `;`, or through the `}`
    // that closes its arms.
    source::ByteSpan span;
    std::vector<ProofArm> arms;

    // The '{' ... '}' enclosing `arms`, for Cases/Decompose/Induction only
    // (empty span otherwise). Layout-only: the formatter is the one consumer,
    // to relocate/canonicalize arm blocks; no semantic reader needs it.
    source::ByteSpan arms_span;
};

// One arm of a `cases` statement (GRAMMAR.md 5.7).
//
// The recognizer reads arm syntax without knowing what the subject is. Which
// case a label denotes, how many binders the case supplies, and whether the
// arms are exhaustive are all settled later against the subject's decomposition
// provider (SPEC.md 20.1, 20.2).
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
    // `omit label by contradiction ev;` (GRAMMAR.md 5.7): the case is accounted
    // for without an arm body. `statements` holds the single contradiction
    // statement, checked under this case's discriminator premise. Kept explicit
    // rather than inferred from the body's shape, because CASE-004 requires a
    // case to be covered by exactly one of an arm or an omission, so the two
    // must stay distinguishable even when a real arm's body is one statement.
    bool omitted = false;
    // For an omission, its `omit` and `by` as written.
    source::ByteSpan omit_keyword;
    source::ByteSpan by_keyword;
    std::vector<std::string> binders;
    std::vector<ProofStatement> statements;

    // The whole arm, from its label's first byte through the closing '}' of
    // its body, inclusive. Layout-only, like `ProofStatement::arms_span`.
    source::ByteSpan span;
    // The '{' ... '}' body alone, for the same reason.
    source::ByteSpan body_span;
    // For an omission, the `contradiction evidence;` statement after `by`, as
    // written. Layout-only too: the formatter copies it verbatim, as it copies
    // every primitive proof statement.
    source::ByteSpan discharge_span;
};

// proof name(parameters) proves (proposition) { statements }   (GRAMMAR.md 4)
struct ProofDeclaration {
    // An inline Law body uses the same proof pipeline and owns only its body
    // span. The Law owns the preceding header; erasure covers both spans.
    std::optional<std::size_t> inline_law;
    std::string name;
    source::SourceLocation name_location;
    source::ByteSpan name_span; // the name as written
    source::SourceRange range;  // the whole declaration, including its body
    // The body, `{` through `}`, or through the end of the text while the `}`
    // is not written. Empty until a body is written.
    source::ByteSpan body;
    Completeness completeness = Completeness::Whole;
    source::SourceLocation keyword_location;
    source::ByteSpan keyword;   // `proof` itself; empty for a Law's own body
    std::uint32_t end_line = 0; // presumed line of the closing '}'
    source::ByteSpan parameters;
    source::ByteSpan proves_keyword; // the 'proves' token itself
    source::ByteSpan proposition;
    source::SourceLocation proposition_location;
    std::vector<ProofStatement> statements;
};

// verified [pure] T f(params) [expects (P)] ensures (Q) { body }  (GRAMMAR.md 6)
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

    // The `template <...>` header introducing this function, where it has one.
    // A contract probe names whatever the clause names, including template
    // parameters, so the probe is emitted under this same header.
    source::ByteSpan template_header;

    // Whether that header is `template <>`: an explicit specialization, which
    // declares no parameters. Such a declaration is one concrete function whose
    // arguments are already fixed, not a template awaiting instantiation, so it
    // is checked directly and its probes are ordinary functions (SPEC.md
    // TEMPLATE-001).
    bool explicit_specialization = false;

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

    // Just inside the body's opening brace. A templated function's probes are
    // templates too, and a template is instantiated only where it is used, so
    // the body names its own probes at its own template arguments to make C++
    // instantiate them alongside it (SPEC.md TEMPLATE-001).
    std::size_t body_open = 0;
    std::uint32_t body_open_line = 0;
    std::uint32_t body_open_column = 0;

    [[nodiscard]] const Clause* postcondition() const;
    [[nodiscard]] std::vector<const Clause*> preconditions() const; // in source order
    // The `decreases` clause asking that the function terminate, if written
    // (SPEC.md TERMINATION-004).
    [[nodiscard]] const Clause* measure() const;
};

// while (condition) invariant (P)... { body }
// for (init; condition; increment) invariant (P)... { body }   (GRAMMAR.md 25, 26)
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

// contradiction evidence;   written as a statement of a verified function's body
//                                             (GRAMMAR.md 5.6, SPEC.md VERIFIED-023)
//
// A claim that no execution reaches this point: the facts established on the
// path to it, together with the named evidence, cannot all hold. It is proof
// syntax inside runtime code, so the runtime program keeps only the statement's
// `;`, an empty statement standing where it was written, and Clang is given
// declarations at the same point that resolve the evidence's arguments in scope.
struct PathContradiction {
    std::size_t function_index = 0; // the verified function whose body holds it
    ProofStatement statement;       // read by the proof-statement parser
    source::ByteSpan span;          // `contradiction` through the terminating `;`
    source::ByteSpan erased;        // the same, less the `;` the program keeps
    std::uint32_t end_line = 0;     // presumed position just past the `;`
    std::uint32_t end_column = 0;

    // A claim written inside an arm of a case split on this path, which that
    // split erases and projects along with itself. `omitted` is the case an
    // `omit label by contradiction evidence;` accounts for: its claim is that
    // the case cannot occur here, an obligation distinct from a runtime path
    // claimed not to occur (SPEC.md CASE-012, CASE-016).
    std::optional<std::size_t> split;
    std::optional<std::string> omitted;
};

// cases subject { arms }   or   decompose subject { arm }
//                  written as a statement of a verified function's body
//                                             (GRAMMAR.md 5.7, SPEC.md CASE-017)
//
// Splits the rest of this runtime path by the state partition of the subject's
// value where it is written, with no runtime control flow: each arm continues
// the path with its case's discriminator as a fact, and binds that case's
// values. Proof syntax inside runtime code, so the program keeps an empty
// statement where it stood, and Clang is given a block at the same point that
// resolves the subject, labels, binders and nested claims in scope.
struct PathCaseSplit {
    std::size_t function_index = 0; // the verified function whose body holds it
    ProofStatement statement;       // `cases` or `decompose`, arms included
    source::ByteSpan span;          // the keyword through the closing `}`
    std::uint32_t end_line = 0;     // presumed position just past the `}`
    std::uint32_t end_column = 0;
    // The claims its arms hold, nested splits' included, in the order a walk
    // of arms and their statements meets them: indices into
    // `Syntax::path_contradictions`.
    std::vector<std::size_t> claims;
};

// unsafe { statements }   (GRAMMAR.md 22, SPEC.md 26)
//
// An explicit boundary around runtime code C++L does not verify. The statements
// run as ordinary C++; only the word `unsafe` leaves the program. In a verified
// body the block is not lowered statement by statement: it is a region whose
// effects are modeled conservatively and which establishes no fact (UNSAFE-003,
// INTERACT-018), so Clang is given a marker declaration just inside its `{`
// that tells the body lowering where the region is.
struct UnsafeBlock {
    // The verified function whose body holds the block, if any. A block in an
    // ordinary function is still an unsafe region of the program, but there is
    // no claim for it to weaken.
    std::optional<std::size_t> function_index;
    source::ByteSpan keyword; // `unsafe` itself
    source::SourceLocation location;
    source::ByteSpan body; // `{` through `}`
    // A block inside another unsafe block is part of that region: it gets no
    // marker of its own, only its word erased.
    bool nested = false;

    // Just past the `{`, where the marker is inserted, and the presumed position
    // the text after it resumes at.
    std::size_t body_open = 0;
    std::uint32_t body_open_line = 0;
    std::uint32_t body_open_column = 0;
};

// unsafe T f(parameters);   (GRAMMAR.md 23, SPEC.md UNSAFE-001, UNSAFE-002)
//
// Every call to the function crosses an unsafe runtime boundary. It is not a
// verified function and supplies no summary; a verified body may call it only
// inside an unsafe block.
struct UnsafeFunction {
    source::ByteSpan keyword;
    source::SourceLocation keyword_location;
    std::string function_name;
    source::SourceLocation function_location;
    std::size_t function_offset = 0;
};

// ghost simple-declaration   (GRAMMAR.md 21, SPEC.md 25)
//
// Proof-only local state in a verified body. The whole declaration leaves the
// program, so nothing that runs may depend on it (GHOST-001, GHOST-002, ERASE-011).
// It stands directly in a block, so no statement is left without a body when it
// goes. Clang is given a marker declaration just before it, which tells the body
// lowering that the declaration after it is ghost.
struct GhostDeclaration {
    std::size_t function_index = 0; // the verified function whose body holds it
    source::ByteSpan keyword;       // `ghost` itself
    source::SourceLocation location;
    source::ByteSpan erased; // from `ghost` through the terminating `;`
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
    source::ByteSpan name_span; // the name as written
    source::SourceRange range;  // the whole declaration, including its ';'
    source::SourceLocation keyword_location;
    source::ByteSpan keyword;       // `type` itself
    source::ByteSpan where_keyword; // the `where` before the predicate
    std::uint32_t end_line = 0;     // presumed line of the terminating ';'

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

// An explicit instantiation definition of a function template,
// `template T f<args>(params);` (SPEC.md TEMPLATE-001).
//
// It instantiates the body in this unit, so the specialization it names has a
// contract to discharge here. Clang's cursor API exposes no cursor for the
// instantiation itself, so the specialization is reached the way every other
// one is -- from a reference to it, which the projector emits into the analysis
// text alone.
//
// `extern template ...` is an instantiation declaration, not a definition: it
// instantiates nothing here and is deliberately not recorded.
struct ExplicitInstantiation {
    std::string function_name;
    source::SourceLocation location;

    // The id-expression naming the specialization, `f<args>` or `C<int>::f`, as
    // written. A reference is built from this spelling rather than rebuilt from
    // parts, so which specialization it denotes stays Clang's to resolve.
    source::ByteSpan id_expression;

    // Just past the `;`, where the reference is emitted.
    std::size_t insertion_offset = 0;
    std::uint32_t insertion_line = 0;
};

struct Syntax {
    std::vector<LawDeclaration> laws;
    std::vector<ProofDeclaration> proofs;
    std::vector<PureMarker> pure_markers;
    std::vector<VerifiedFunction> verified_functions;
    std::vector<LoopSpecification> loops;
    std::vector<PathContradiction> path_contradictions;
    std::vector<PathCaseSplit> path_splits;
    std::vector<RefinementType> refinement_types;
    std::vector<ExplicitInstantiation> explicit_instantiations;
    std::vector<UnsafeBlock> unsafe_blocks;
    std::vector<UnsafeFunction> unsafe_functions;
    std::vector<GhostDeclaration> ghost_declarations;

    // A specification clause written on a function that is not 'verified'
    // (GRAMMAR.md 6 permits the syntax; this implementation does not check
    // such a contract - `has_specification_clause`'s diagnostic explains
    // why). Never an obligation and never consumed by elaboration/obligation
    // generation: this exists only so the formatter/style-checker can lay
    // out clause syntax a developer actually wrote, the same way it lays out
    // any other syntactically well-formed, semantically unsupported
    // construct. Populated independent of `RecognitionMode`.
    std::vector<VerifiedFunction> unchecked_clauses;

    [[nodiscard]] bool empty() const noexcept {
        return laws.empty() && proofs.empty() && pure_markers.empty() && verified_functions.empty() && loops.empty() &&
               path_contradictions.empty() && path_splits.empty() && refinement_types.empty() &&
               unchecked_clauses.empty() && unsafe_blocks.empty() && unsafe_functions.empty() &&
               ghost_declarations.empty();
    }
};

// Recognizes C++L constructs in a preprocessed token stream.
//
// Recognition is contextual: a C++L word is only a C++L word where the
// complete grammatical context makes the ordinary C++ reading impossible
// (SPEC.md 3, 3.1). Ordinary declarations such as `int law = 1;` are left
// untouched.
//
// Compile keeps only what compiles. Edit also keeps a malformed declaration,
// and one outside namespace scope, for the formatter to lay out. Draft is Edit
// for text still being written: it also keeps a Law or a proof the author has
// not finished (Completeness), and reads past a proof statement it cannot read
// (ProofStatementKind::Unread), so an editor asks the recognizer where the
// author is rather than reading the grammar itself.
enum class RecognitionMode : std::uint8_t { Compile, Edit, Draft };

[[nodiscard]] Syntax recognize(const TokenStream& stream, diagnostics::Engine& engine,
                               RecognitionMode mode = RecognitionMode::Compile);

// Where `syntax` holds what only a draft keeps: a Law or a proof not written
// whole, or a proof statement the recognizer could not read. Nothing, for a
// syntax recognized to be compiled or formatted. A draft is where the author
// is, never what the program means, so nothing that decides meaning accepts
// one (ARCHITECTURE.md ARCH-LSP-007).
[[nodiscard]] std::optional<source::SourceLocation> draft_only(const Syntax& syntax);

} // namespace cppl::frontend
