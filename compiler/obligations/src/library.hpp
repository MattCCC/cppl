#pragma once

#include "cppl/obligations/contracts.hpp"
#include "cppl/vir/expr.hpp"
#include "lowering.hpp"

#include <expected>

namespace cppl::obligations::detail {

// The trusted summary of the standard-library operation a library call makes
// (SPEC.md STDMODEL-013, RFC 0020 §6), stated at the kernel types of the call's
// own arguments and result.
//
// Every statement here is an assumption about the library, never a derivation:
// this is the one place the model says what `push_back` and the others do, and
// TRUST.md 28.1 lists it word for word. A call whose shape does not match what
// its operation is stated for is refused rather than given a guess.
[[nodiscard]] std::expected<LibrarySummary, Failure> library_summary(const vir::Call& call, const vir::Expr& site);

} // namespace cppl::obligations::detail
