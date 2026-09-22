#pragma once

#include <cstdint>
#include <vector>

namespace cppl::source {

// Describes analysis scaffolding, not logical evidence. The bridge checks the
// expected C++ shape and resolves its leaves and binders through Clang. The
// elaborator must still construct typed VIR, and only the kernel checks proofs.
enum class ProjectionKind : std::uint8_t {
    Expression,
    Equality,
    Universal,
    Implication,
    Conjunction,
    Disjunction,
    Equivalence,
    // `readable(p)` / `writable(p, n)`: a built-in memory proposition, not a
    // call to a user function (SPEC.md 12.10). It never becomes a kernel
    // proposition and never becomes a runtime call; the bridge resolves its
    // operands through Clang and hands the obligation layer a capability.
    Readable,
    Writable,
    // A conjunction whose operands are all memory capabilities. A contract
    // states one `expects` clause, so several capabilities reach it joined by
    // `&&`; the clause is still entirely a capability statement and still never
    // reaches the kernel.
    Capabilities
};

struct ProjectionShape {
    ProjectionKind kind = ProjectionKind::Expression;
    std::vector<ProjectionShape> children;
};

} // namespace cppl::source
