#pragma once

#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"

#include <cstdint>

namespace cppl::kernel {

// Raises every variable free at or above `cutoff` by `amount`.
//
// A term stated outside a binder means something different underneath it: the
// binder shifts every enclosing index by one. Shifting restates the term for
// the deeper context so that moving it there changes nothing about which
// binders its variables denote.
[[nodiscard]] Term shift(const Term& term, std::uint32_t amount, std::uint32_t cutoff = 0);

// The same restatement for a proposition, needed where one is carried across a
// binder: a premise assumed outside a quantifier is used underneath it.
[[nodiscard]] Proposition shift(const Proposition& proposition, std::uint32_t amount, std::uint32_t cutoff = 0);

// Replaces the binder `depth` levels out with `argument`, lowering the
// variables above it to close the gap the binder leaves.
//
// `argument` is stated outside that binder, so it is shifted by the number of
// binders it descends through. That is what makes the substitution capture
// safe: a variable the argument mentions keeps denoting the binder it denoted
// before, and never the one it lands underneath.
[[nodiscard]] Term instantiate(const Term& body, const Term& argument, std::uint32_t depth = 0);

[[nodiscard]] Proposition instantiate(const Proposition& body, const Term& argument, std::uint32_t depth = 0);

} // namespace cppl::kernel
