#pragma once

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

// The one checked-contradiction mechanism (SPEC.md CASE-011, CASE-013).
//
// Everything that claims a context cannot occur - a `contradiction` statement,
// a case omitted from `cases`, and an unreachable runtime path - is discharged
// here, so the three claims share one account of what a contradiction is while
// each caller keeps its own origin, provenance and diagnostics (CASE-016).
//
// Nothing here adds a rule. The proposition language has no falsity constant,
// and no existing rule derives an arbitrary proposition from a false one, so a
// contradiction is carried by an equality no value satisfies and discharged by
// the rule that already reasons from facts: linear arithmetic, which refutes
// `F1 /\ ... /\ Fn /\ not G`. Every term built here is ordinary kernel evidence,
// and every certificate in it is rechecked by the kernel against the system the
// kernel states for itself (CASE-014). Nothing found is never a contradiction
// (CASE-015).
namespace cppl::obligations::detail {

// `0 == 1` over booleans: the proposition a derived contradiction concludes.
[[nodiscard]] kernel::Proposition absurdity();

// Why no contradiction was established.
struct Unestablished {
    enum class Kind : std::uint8_t {
        // A fact, or the goal, is not something linear arithmetic can state.
        Unreadable,
        // The search found no refutation. This is never a finding that the
        // facts are satisfiable: the search is bounded, so this is only the
        // absence of evidence, and callers report it as an unproven claim.
        NotFound,
    };
    Kind kind = Kind::NotFound;
    std::string detail;
};

// Whether `proposition` can stand as a fact in a linear-arithmetic step.
[[nodiscard]] bool arithmetic_fact(const kernel::Context& context, const kernel::Proposition& proposition);

// Evidence of `absurdity()` from `facts`.
//
// The goal takes no part in this step. Its negation holds outright, so a
// certificate can only refute the facts themselves: this establishes that the
// facts are contradictory, never merely that some goal follows from them. That
// distinction is the whole difference between omitting a case because it
// cannot occur and closing it because its goal happened to be provable.
[[nodiscard]] std::expected<kernel::ProofTerm, Unestablished> refute_facts(const kernel::Context& context,
                                                                           std::vector<kernel::ArithmeticFact> facts);

// Evidence of `goal` from `absurd`, evidence of `absurdity()`, in a context
// where `absurd` is well formed. `goal` may have any shape: its quantifiers,
// premises, conjuncts and one disjunct are introduced, and each equality left
// is closed from the contradiction. Those equalities must be of integers,
// because linear arithmetic states nothing else.
[[nodiscard]] std::expected<kernel::ProofTerm, Unestablished> from_absurdity(const kernel::Context& context,
                                                                             kernel::ProofTerm absurd,
                                                                             const kernel::Proposition& goal);

} // namespace cppl::obligations::detail
