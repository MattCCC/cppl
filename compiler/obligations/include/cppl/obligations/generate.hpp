#pragma once

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/obligations/interface.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/vir/module.hpp"

#include <functional>
#include <optional>

namespace cppl::obligations {

// Candidate evidence for a closed claim, from automation.
//
// `induction x;` asks automation for evidence in each of its cases (SPEC.md
// INDUCT-005). Obligation generation builds a written proof's evidence but does
// not depend on automation, so the caller supplies it here. What it returns is
// a candidate and nothing more: it is checked by the kernel against the case's
// claim before it is used, and the whole proof is checked again with it.
using CaseAutomation =
    std::function<std::optional<kernel::ProofTerm>(const kernel::Context&, const kernel::Proposition&)>;

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
//
// With no `automation`, a short-form `induction x;` is refused rather than
// given evidence some other way.
[[nodiscard]] Program generate(const vir::Module& module, const elaboration::Result& elaborated,
                               diagnostics::Engine& engine, const Imports& imports = {},
                               const CaseAutomation& automation = {});

} // namespace cppl::obligations
