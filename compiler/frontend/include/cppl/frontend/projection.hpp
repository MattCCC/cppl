#pragma once

#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/projection.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace cppl::frontend {

// The ordinary C++ function a Law is projected into so that Clang resolves its
// specification expression: name lookup, overload resolution, conversions and
// canonical types all come from Clang rather than from C++L (SPEC.md 7.3).
//
// It carries the Law's own name, so a Law occupies a formal declaration
// namespace associated with its C++ scope and a proof can name it through
// ordinary C++ lookup (GRAMMAR.md 46).
struct SpecificationFunction {
    std::string name;
    std::size_t law_index = 0;

    // The generated function stating the Law's precondition, empty when the Law
    // has none. It is generated rather than named after the Law, because only
    // the Law's conclusion is what a proof names.
    std::string premise_name;
    std::size_t analysis_offset = 0;    // name token in the physical analysis buffer
    std::string proposition_probe = {}; // unique projection identity, independent of #line
};

// The ordinary C++ function a proof declaration's `proves` clause is projected
// into. Its body is the proposition the proof claims; the proof statements
// themselves are C++L and are never projected into C++.
struct ProofFunction {
    std::string name;
    std::size_t proof_index = 0;

    // One generated function per term the proof's statements instantiate their
    // references at, in written order. Each returns that term, so Clang decides
    // what the term denotes and what type it has.
    std::vector<std::string> argument_names;

    // One generated function per `assume` statement, in written order, stating
    // the proposition that statement names.
    std::vector<std::string> assumption_names;
    // Subjects and named labels of cases, in depth-first source order.
    std::vector<std::string> case_names;
};

// The ordinary C++ functions a verified function's contract is projected into.
//
// The postcondition takes the verified function's parameters and one more, of
// its return type, named `result`. That is what makes `result` an ordinary name
// Clang resolves; it is never introduced into the program itself.
struct ContractFunctions {
    std::size_t function_index = 0;
    std::string postcondition_name;
    std::vector<std::string> precondition_names; // one per expects clause, in source order
};

// The declaration a loop invariant is projected into: a generated `bool` local
// at the start of the loop body, initialized with the invariant, so Clang
// resolves it in the scope the loop head sees. It exists only in the analysis
// text, and the bridge reads it back as the loop's invariant rather than as a
// statement of the body.
// A loop clause resolved in the loop head's scope. An invariant is a condition;
// a measure is the integer expression of a `decreases` clause (SPEC.md 24.3).
struct LoopInvariantMarker {
    std::string name;
    std::size_t loop_index = 0;
    std::size_t function_index = 0;
    bool measure = false;
    source::SourceLocation location;
};

// The declarations a claim that a path cannot occur is projected into
// (SPEC.md VERIFIED-023): a block standing where the statement was written,
// holding a `bool` named `name` and then one declaration per argument of the
// evidence, `name` followed by `_argument_` and its position, initialized with
// that argument. Clang resolves each argument in the scope the statement sees,
// and the bridge reads the block back as the end of the path rather than as
// statements of the body. It exists only in the analysis text.
struct PathContradictionMarker {
    std::string name;
    std::size_t claim_index = 0; // into Syntax::path_contradictions
    std::size_t function_index = 0;
    source::SourceLocation location;
};

// A formal equality is never represented by a C++ operator== or a fabricated
// Eq template. Its analysis-only probe asks Clang to resolve a two-parameter
// lambda call at the stated type. The bridge reads the resolved arguments,
// while the proposition itself is supplied by this explicit projection record.
struct PropositionProbe {
    std::string owner;
    std::string name;
    source::SourceLocation location;
    source::ProjectionShape shape;
};

// A refinement type's predicate, projected so Clang resolves it with `self`
// bound to a value of the base type and each index bound to its own parameter.
// The proposition itself is supplied by this record; the probe only asks Clang
// what the expression means (SPEC.md 17.1).
struct RefinementProbe {
    std::string name;  // the refinement type's own name
    std::string probe; // the generated function stating its predicate
    std::size_t refinement_index = 0;
    std::size_t index_count = 0; // parameters standing before `self`
    source::SourceLocation location;
    source::ProjectionShape shape;
    std::size_t alias_offset = 0; // generated alias name in the analysis buffer
};

// A runtime-bearing C++L declaration and the canonical C++ it lowers to
// (`TRUST.md` 29.1). The text is recomputed from the declaration when erasure is
// checked, so the projector cannot put anything else in its place.
struct RuntimeLowering {
    source::ByteSpan span; // the declaration in the scanned text
    std::string text;      // the canonical C++ that replaces it
};

// One projector, two texts.
//
// `runtime` is the program: the scanned text with every proof-only span blanked
// and every runtime-bearing declaration replaced by the canonical C++ it means.
// `analysis` is the same text with those spans replaced by the specification
// functions Clang needs to resolve. Both come from the same spans in the same
// pass, so the runtime program C++L verifies and the runtime program Clang
// compiles cannot drift apart (ARCHITECTURE.md 8, 10).
//
// Blanking preserves every byte position and every line of the text that
// remains. A canonical lowering preserves every line, so no line number moves,
// and introduces only the declaration C++ already has a spelling for, which is
// what keeps a C++17 target C++17 (COMPATIBILITY.md).
struct BindingProbe {
    std::string key;
    std::string subject;
    std::string label;
    std::size_t index = 0;
    bool product = false;
    source::SourceLocation location;
};

struct Projection {
    std::string analysis;
    std::string runtime;
    std::vector<SpecificationFunction> specification_functions;
    std::vector<ProofFunction> proof_functions;
    std::vector<ContractFunctions> contract_functions;
    std::vector<LoopInvariantMarker> loop_invariants;
    std::vector<PathContradictionMarker> path_contradictions;
    std::vector<PropositionProbe> proposition_probes;
    std::vector<RefinementProbe> refinement_probes;
    std::vector<RuntimeLowering> runtime_lowerings;
    std::vector<BindingProbe> binding_probes;
    std::vector<diagnostics::Diagnostic> diagnostics;

    // Positions of executable declarations copied into the analysis buffer.
    // Presumed file/line/column are diagnostic labels and may be repeated by
    // #line. They cannot identify which declaration was actually selected.
    struct DeclarationOffset {
        std::size_t original = 0;
        std::size_t analysis = 0;
    };
    std::vector<DeclarationOffset> declaration_offsets;
    [[nodiscard]] std::optional<std::size_t> declaration_offset(std::size_t original) const;
};

struct ProjectionOptions {
    std::string generated_prefix = "__cppl_";
    std::string unit_key; // distinguishes generated names between units
    std::map<std::string, std::string> binding_types;
    std::set<std::size_t> void_functions = {}; // Clang-resolved return types
};

[[nodiscard]] Projection project(const TokenStream& stream, const Syntax& syntax, const ProjectionOptions& options);

// The canonical C++ a refinement declaration lowers to (SPEC.md 17.8):
//
//     type R = T where (P);              ->  using R = T;
//     type R(I i) = T where (P);         ->  template <I i> using R = T;
//
// Deterministic and derived from the declaration alone, so erasure can check the
// runtime program against it without trusting the projector. The result carries
// one newline per newline in the declaration, so no line moves. An index written
// without a type takes the base type.
[[nodiscard]] std::string canonical_lowering(const TokenStream& stream, const RefinementType& refinement);

} // namespace cppl::frontend
