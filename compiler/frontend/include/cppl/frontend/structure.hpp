#pragma once

#include "cppl/frontend/syntax.hpp"
#include "cppl/source/location.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cppl::frontend {

// The shape of the C++L a syntax holds, as the recognizer found it, for an
// editor to fold and select by. An editor never finds a body, a clause or a
// statement by reading the text itself (ARCHITECTURE.md ARCH-LSP-006); the
// structure of ordinary C++ is Clang's to say.

// A region of C++L a reader may fold away.
struct Block {
    enum class Kind : std::uint8_t {
        // `{` through `}`: a proof's body, the arms of a `cases`, `decompose`
        // or `induction`, and each arm's body.
        Braces,
        // A declaration with no body of its own, from its first word through
        // its `;`: a Law stated by its clauses, a refinement type.
        Declaration,
    };
    Kind kind = Kind::Braces;
    source::ByteSpan span;
};

[[nodiscard]] std::vector<Block> blocks(const Syntax& syntax);

// Every span of C++L that holds byte `offset`, a span's end included, from the
// innermost to the outermost. They run from a name or an expression, through
// the clause, the statement, the arm and the body holding it, to the whole
// declaration.
[[nodiscard]] std::vector<source::ByteSpan> enclosing(const Syntax& syntax, std::size_t offset);

} // namespace cppl::frontend
