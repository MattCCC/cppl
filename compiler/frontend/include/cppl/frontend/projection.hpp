#pragma once

#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"

#include <cstddef>
#include <optional>
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
    std::size_t analysis_offset = 0; // name token in the physical analysis buffer
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
};

// The ordinary C++ functions a verified function's contract is projected into.
//
// The postcondition takes the verified function's parameters and one more, of
// its return type, named `result`. That is what makes `result` an ordinary name
// Clang resolves; it is never introduced into the program itself.
struct ContractFunctions {
    std::size_t function_index = 0;
    std::string postcondition_name;
    std::string precondition_name; // empty when the function states none
};

// The declaration a loop invariant is projected into: a generated `bool` local
// at the start of the loop body, initialized with the invariant, so Clang
// resolves it in the scope the loop head sees. It exists only in the analysis
// text, and the bridge reads it back as the loop's invariant rather than as a
// statement of the body.
struct LoopInvariantMarker {
    std::string name;
    std::size_t loop_index = 0;
    std::size_t function_index = 0;
    source::SourceLocation location;
};

// One projector, two texts.
//
// `runtime` is the program: the scanned text with every C++L-only span blanked.
// `analysis` is the same text with those spans replaced by the specification
// functions Clang needs to resolve. Both come from the same spans in the same
// pass, so the runtime program C++L verifies and the runtime program Clang
// compiles cannot drift apart (ARCHITECTURE.md 10, 11).
//
// Blanking preserves every byte position and every line of the text that
// remains, so erasure can only delete: no construct is ever inserted into the
// runtime program, which is what keeps a C++17 target C++17
// (COMPATIBILITY.md).
struct Projection {
    std::string analysis;
    std::string runtime;
    std::vector<SpecificationFunction> specification_functions;
    std::vector<ProofFunction> proof_functions;
    std::vector<ContractFunctions> contract_functions;
    std::vector<LoopInvariantMarker> loop_invariants;

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
};

[[nodiscard]] Projection project(const TokenStream& stream, const Syntax& syntax, const ProjectionOptions& options);

} // namespace cppl::frontend
