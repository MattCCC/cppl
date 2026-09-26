#pragma once

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/obligations/interface.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/vir/module.hpp"

namespace cppl::obligations {

// Turns VIR and the Laws stated over it into explicit proof obligations, and
// admits the definitions the kernel is allowed to unfold while checking them
// (ARCHITECTURE.md 51).
//
// A construct that cannot be lowered produces a diagnostic and no obligation.
// It never produces a weaker obligation than the one the Law states.
//
// A verified function this unit declares and does not define is established
// only from `imports`, and only when an entry records the statement this unit
// builds from its own declaration (SPEC.md TUBOUND-003, TUBOUND-004); otherwise it is
// refused.
[[nodiscard]] Program generate(const vir::Module& module, const elaboration::Result& elaborated,
                               diagnostics::Engine& engine, const Imports& imports = {});

} // namespace cppl::obligations
