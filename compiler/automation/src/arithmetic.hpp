#pragma once

#include "cppl/kernel/check.hpp"
#include "cppl/kernel/linear.hpp"

#include <optional>

namespace cppl::automation {

// Searches for a certificate that `system` has no integer solution.
//
// Fourier-Motzkin elimination over the rationals finds the contradictions;
// the disjunctions of the system, and the wrap multiples of its arithmetic,
// are split case by case until each case has one. The search is bounded and
// untrusted: whatever it returns goes to the kernel, which checks it against
// the system it states itself.
[[nodiscard]] std::optional<kernel::ArithmeticCertificate> refute(const kernel::ArithmeticSystem& system);

// Evidence for `goal` that introduces its quantifiers and premises and closes
// the equality underneath by linear arithmetic over the premises. With
// `rewriting`, equalities among the premises, and equalities between variables
// that arithmetic establishes, are first used to rewrite the goal.
[[nodiscard]] std::optional<kernel::ProofTerm> arithmetic_evidence(const kernel::Context& context,
                                                                   const kernel::Proposition& goal, bool rewriting);

} // namespace cppl::automation
