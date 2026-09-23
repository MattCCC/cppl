#pragma once

#include "cppl/kernel/box.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"

#include <string>
#include <utility>
#include <variant>

namespace cppl::kernel {

struct Proposition;

// Propositional equality (SPEC.md 7.2). Eq is a proposition, not a Boolean
// value: it is established by proof evidence, never by evaluation to `true`.
struct Eq {
    Type type;
    Term lhs;
    Term rhs;

    friend bool operator==(const Eq&, const Eq&) = default;
};

// Universal quantification (SPEC.md 8). The bound variable is referenced from
// the body as the innermost de Bruijn index.
struct Forall {
    Type binder;
    Box<Proposition> body;

    friend bool operator==(const Forall&, const Forall&) = default;
};

// Implication (SPEC.md 8.2, GRAMMAR.md 29). `premise -> conclusion` is
// established by evidence for the conclusion that may use the premise, and it
// is never a claim that the premise holds.
//
// The premise binds no term variable, so de Bruijn indices mean the same on
// both sides of the arrow.
struct Implies {
    Box<Proposition> premise;
    Box<Proposition> conclusion;

    friend bool operator==(const Implies&, const Implies&) = default;
};

// Conjunction (SPEC.md 7.6, GRAMMAR.md 33). `left && right` is established by
// evidence for each side, and evidence for it establishes either side.
//
// Neither side binds a term variable, so de Bruijn indices mean the same in
// both.
struct And {
    Box<Proposition> left;
    Box<Proposition> right;

    friend bool operator==(const And&, const And&) = default;
};

// Disjunction (SPEC.md 7.8, GRAMMAR.md 33). `left || right` is established by
// evidence for one of its sides, and evidence for it establishes only what both
// sides establish, so using it means proving the goal under each side.
//
// Neither side binds a term variable, so de Bruijn indices mean the same in
// both.
struct Or {
    Box<Proposition> left;
    Box<Proposition> right;

    friend bool operator==(const Or&, const Or&) = default;
};

// Falsity (FOUNDATIONS.md 26). No rule introduces it: evidence for it exists only
// where a hypothesis supposes it or linear arithmetic refutes the facts alone,
// and its one use is eliminating it into any goal.
struct Falsity {
    friend bool operator==(const Falsity&, const Falsity&) = default;
};

struct Proposition {
    std::variant<Eq, Forall, Implies, And, Or, Falsity> node;

    static Proposition equality(Type type, Term lhs, Term rhs) {
        return Proposition{Eq{std::move(type), std::move(lhs), std::move(rhs)}};
    }

    static Proposition for_all(Type binder, Proposition body) {
        return Proposition{Forall{std::move(binder), Box<Proposition>{std::move(body)}}};
    }

    static Proposition implication(Proposition premise, Proposition conclusion) {
        return Proposition{Implies{Box<Proposition>{std::move(premise)}, Box<Proposition>{std::move(conclusion)}}};
    }

    static Proposition conjunction(Proposition left, Proposition right) {
        return Proposition{And{Box<Proposition>{std::move(left)}, Box<Proposition>{std::move(right)}}};
    }

    static Proposition disjunction(Proposition left, Proposition right) {
        return Proposition{Or{Box<Proposition>{std::move(left)}, Box<Proposition>{std::move(right)}}};
    }

    static Proposition falsity() {
        return Proposition{Falsity{}};
    }

    friend bool operator==(const Proposition&, const Proposition&) = default;
};

std::string describe(const Proposition& proposition);

// Requires a well-typed boolean term; the checker validates it before use.
Proposition predicate(const Term& condition, bool positive);

} // namespace cppl::kernel
