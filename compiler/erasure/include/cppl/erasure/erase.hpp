#pragma once

#include <cstddef>
#include <string_view>

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"

namespace cppl::erasure {

struct Report {
    std::size_t erased_spans = 0;
    std::size_t erased_bytes = 0;

    // The checked properties. Both must hold for the runtime program to be
    // accepted for code generation.
    bool only_deletions = false;   // no byte was added or altered, only blanked
    bool lines_preserved = false;  // every line of the remaining program is where it was
};

struct Erased {
    std::string_view runtime;
    Report report;
};

// Selects the runtime program and checks that erasure did what it claims.
//
// The property checked here is stronger than "the formal syntax is gone": the
// runtime text must be the scanned text with C++L-only spans blanked and
// nothing else changed. That is what makes erasure unable to alter runtime
// behaviour, and unable to introduce a construct the target standard does not
// have (TRUST.md 7, COMPATIBILITY.md).
[[nodiscard]] Erased erase(const frontend::TokenStream& stream,
                           const frontend::Syntax& syntax,
                           const frontend::Projection& projection,
                           diagnostics::Engine& engine);

}  // namespace cppl::erasure
