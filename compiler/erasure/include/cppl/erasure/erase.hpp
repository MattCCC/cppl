#pragma once

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"

#include <cstddef>
#include <string_view>

namespace cppl::erasure {

struct Report {
    std::size_t erased_spans = 0;
    std::size_t erased_bytes = 0;

    // Runtime-bearing declarations, and the bytes of canonical C++ they lowered
    // to (TRUST.md 7.1).
    std::size_t lowered_spans = 0;
    std::size_t lowered_bytes = 0;

    // The checked properties. All three must hold for the runtime program to be
    // accepted for code generation.
    bool only_deletions = false;      // outside a lowering, no byte was added or altered
    bool lines_preserved = false;     // every line of the remaining program is where it was
    bool lowerings_canonical = false; // each lowering is exactly what its declaration means
};

struct Erased {
    std::string_view runtime;
    Report report;
};

// Selects the runtime program and checks that erasure did what it claims.
//
// The property checked here is stronger than "the formal syntax is gone". C++L
// syntax falls into two classes (TRUST.md 7):
//
//   - proof-only syntax is blanked, so no byte is added or altered;
//   - a runtime-bearing declaration is replaced by the canonical C++ it means,
//     which this function recomputes from the declaration itself rather than
//     taking the projector's word for it.
//
// Every line stays where it was in both cases. That is what makes erasure unable
// to alter runtime behaviour, and unable to introduce a construct the target
// standard does not have (TRUST.md 7, COMPATIBILITY.md).
[[nodiscard]] Erased erase(const frontend::TokenStream& stream, const frontend::Syntax& syntax,
                           const frontend::Projection& projection, diagnostics::Engine& engine);

} // namespace cppl::erasure
