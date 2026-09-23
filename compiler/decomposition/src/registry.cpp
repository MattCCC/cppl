#include "cppl/decomposition/decomposition.hpp"
#include "cppl/decomposition/providers.hpp"
#include "cppl/vir/types.hpp"

#include <array>

namespace cppl::decomposition {
namespace {

// Provider order is fixed so decomposition is deterministic. Providers
// recognize disjoint representations, so order does not decide meaning; it is
// fixed because verification must be reproducible (AGENTS.md 21).
std::array<const Provider*, 2> providers() {
    return {&scoped_enum_provider(), &structural_provider()};
}

} // namespace

const Provider* provider_for(const vir::Type& type) {
    for (const Provider* provider : providers()) {
        if (provider->recognizes(type)) {
            return provider;
        }
    }
    return nullptr;
}

Decomposition decompose(const Subject& subject) {
    const vir::Type& type = subject.expression.type;
    if (const Provider* provider = provider_for(type)) {
        return provider->decompose(subject);
    }

    // Failing here, at the provider boundary, is what keeps the engine
    // representation-independent: no representation is reinterpreted as another
    // because a proof used arm syntax on it.
    return Unsupported{type.representation.name.empty() ? describe(type) : type.representation.name,
                       "no decomposition provider models this resolved C++ representation"};
}

} // namespace cppl::decomposition
