#pragma once

#include <string>
#include <variant>

#include "cppl/kernel/box.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"

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

struct Proposition {
    std::variant<Eq, Forall> node;

    static Proposition equality(Type type, Term lhs, Term rhs) {
        return Proposition{Eq{std::move(type), std::move(lhs), std::move(rhs)}};
    }

    static Proposition for_all(Type binder, Proposition body) {
        return Proposition{Forall{std::move(binder), Box<Proposition>{std::move(body)}}};
    }

    friend bool operator==(const Proposition&, const Proposition&) = default;
};

std::string describe(const Proposition& proposition);

}  // namespace cppl::kernel
