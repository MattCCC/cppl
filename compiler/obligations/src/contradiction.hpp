#pragma once

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>

// The one checked-contradiction mechanism (SPEC.md CASE-011, CASE-013).
//
// Everything that claims a context cannot occur - a `contradiction` statement,
// a case omitted from `cases`, and an unreachable runtime path - is discharged
// here, so the three claims share one account of what a contradiction is while
// each caller keeps its own origin, provenance and diagnostics (CASE-016).
//
// A contradiction is evidence for `False`: linear arithmetic refuting the named
// evidence and the premises standing where it is written, with no goal taking
// part. What it discharges is then closed by the kernel's falsity elimination,
// whatever its shape (FOUNDATIONS.md 26). Every term built here is ordinary
// kernel evidence, and every certificate in it is rechecked by the kernel
// against the system the kernel states for itself (CASE-014). Nothing found is
// never a contradiction (CASE-015).
namespace cppl::obligations::detail {

// Why no contradiction was established.
struct Unestablished {
    enum class Kind : std::uint8_t {
        // A fact is not something linear arithmetic can state.
        Unreadable,
        // The search found no refutation. This is never a finding that the
        // facts are satisfiable: the search is bounded, so this is only the
        // absence of evidence, and callers report it as an unproven claim.
        NotFound,
    };
    Kind kind = Kind::NotFound;
    std::string detail;
};

// A premise standing where a contradiction is claimed, with the evidence that
// reaches it there: a hypothesis, stated at the depth the claim is made at.
struct Standing {
    kernel::Proposition proposition;
    kernel::ProofTerm evidence;
};

// Whether `proposition` can stand as a fact in a linear-arithmetic step.
[[nodiscard]] bool arithmetic_fact(const kernel::Context& context, const kernel::Proposition& proposition);

// Evidence of `False` from `named`, the evidence a claim names, and the premises
// `standing` where it is made, innermost first.
//
// No goal takes part: a certificate can only refute the facts themselves, so
// this establishes that they are contradictory, never merely that some goal
// follows from them. That distinction is the whole difference between claiming
// a context cannot occur and closing it because its goal happened to be
// provable.
//
// A conjunction contributes each conjunct. A premise linear arithmetic cannot
// state takes no part, and neither does one past the core's limit on facts,
// which is why the innermost - an omitted case's own discriminator, a runtime
// path's latest condition - come first: leaving a premise out can only fail to
// find a contradiction, never make one appear.
[[nodiscard]] std::expected<kernel::ProofTerm, Unestablished> refute(const kernel::Context& context,
                                                                     kernel::ArithmeticFact named,
                                                                     std::span<const Standing> standing);

} // namespace cppl::obligations::detail
