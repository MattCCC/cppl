#pragma once

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/vir/module.hpp"

namespace cppl::obligations {

// Turns VIR and the Laws stated over it into explicit proof obligations, and
// admits the definitions the kernel is allowed to unfold while checking them
// (ARCHITECTURE.md 19).
//
// A construct that cannot be lowered produces a diagnostic and no obligation.
// It never produces a weaker obligation than the one the Law states.
[[nodiscard]] Program generate(const vir::Module& module, const elaboration::Result& elaborated,
                               diagnostics::Engine& engine);

} // namespace cppl::obligations
