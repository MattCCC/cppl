#pragma once

#include <cstdint>
#include <vector>

namespace cppl::source {

// Describes analysis scaffolding, not logical evidence. The bridge checks the
// expected C++ shape and resolves its leaves and binders through Clang. The
// elaborator must still construct typed VIR, and only the kernel checks proofs.
enum class ProjectionKind : std::uint8_t { Expression, Equality, Universal, Implication, Conjunction, Equivalence };

struct ProjectionShape {
    ProjectionKind kind = ProjectionKind::Expression;
    std::vector<ProjectionShape> children;
};

} // namespace cppl::source
