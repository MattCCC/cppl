#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"

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
};

// The ordinary C++ function a proof declaration's `proves` clause is projected
// into. Its body is the proposition the proof claims; the proof statements
// themselves are C++L and are never projected into C++.
struct ProofFunction {
    std::string name;
    std::size_t proof_index = 0;
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
};

struct ProjectionOptions {
    std::string generated_prefix = "__cppl_";
    std::string unit_key;  // distinguishes generated names between units
};

[[nodiscard]] Projection project(const TokenStream& stream,
                                 const Syntax& syntax,
                                 const ProjectionOptions& options);

}  // namespace cppl::frontend
