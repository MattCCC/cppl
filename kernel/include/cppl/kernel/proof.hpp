#pragma once

#include <string>
#include <variant>

#include "cppl/kernel/box.hpp"
#include "cppl/kernel/types.hpp"

namespace cppl::kernel {

struct ProofTerm;

// refl : Eq<T>(a, b), admissible only when a and b are definitionally equal
// (SPEC.md 16). The kernel decides definitional equality itself; the producer
// of the evidence has no say in it.
struct Reflexivity {
    friend bool operator==(const Reflexivity&, const Reflexivity&) = default;
};

// Introduction of a universal quantifier: evidence for the body under an
// arbitrary inhabitant of `binder`. The binder is restated by the evidence and
// is checked against the goal.
struct ForallIntroduction {
    Type binder;
    Box<ProofTerm> body;

    friend bool operator==(const ForallIntroduction&, const ForallIntroduction&) = default;
};

struct ProofTerm {
    std::variant<Reflexivity, ForallIntroduction> node;

    static ProofTerm reflexivity() { return ProofTerm{Reflexivity{}}; }

    static ProofTerm forall_introduction(Type binder, ProofTerm body) {
        return ProofTerm{ForallIntroduction{std::move(binder), Box<ProofTerm>{std::move(body)}}};
    }

    friend bool operator==(const ProofTerm&, const ProofTerm&) = default;
};

std::string describe(const ProofTerm& proof);

}  // namespace cppl::kernel
