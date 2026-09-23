#pragma once

#include "cppl/kernel/linear.hpp"
#include "cppl/kernel/proof.hpp"

#include <optional>

namespace cppl::refutation {

// A certificate that `system` has no integer solution, or nothing.
//
// Fourier-Motzkin elimination over the rationals finds the contradictions; the
// disjunctions of the system, and the wrap multiples of its arithmetic, are
// split case by case until each case has one.
//
// This is search, so what it returns is a suggestion. A consumer places it in a
// proof term and the kernel checks it against the system the kernel states for
// itself; a wrong certificate is rejected there, never believed here. Nothing
// returned does not mean the system is satisfiable: the search is bounded, and
// running out of budget looks the same as finding no refutation. No caller may
// read that absence as a fact about the system.
[[nodiscard]] std::optional<kernel::ArithmeticCertificate> refute(const kernel::ArithmeticSystem& system);

} // namespace cppl::refutation
