#pragma once

#include "cppl/decomposition/decomposition.hpp"

// The representation providers the registry consults, in order.
//
// Adding a C++ representation to proof-side case reasoning means adding one
// sound provider here and its semantic tests. No other part of the case
// pipeline changes: arm matching, binder handling, exhaustiveness validation,
// proof-state splitting, evidence construction, dependency checking,
// diagnostics, erasure and kernel lowering are representation-independent.
namespace cppl::decomposition {

const Provider& scoped_enum_provider();
const Provider& structural_provider();

} // namespace cppl::decomposition
