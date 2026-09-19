#pragma once

#include <string>
#include <variant>

#include "cppl/kernel/box.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
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

// Elimination of a universal quantifier: evidence for `quantified`, used at one
// particular `argument` (SPEC.md 8).
//
// The evidence restates the proposition it is eliminated from, because nothing
// else in a proof term records it. That restatement is checked, never believed:
// the kernel verifies the evidence against it, verifies it is quantified,
// verifies the argument inhabits the quantified type, and derives the resulting
// proposition itself by substitution.
struct ForallElimination {
    Box<Proposition> quantified;
    Box<ProofTerm> evidence;
    Term argument;

    friend bool operator==(const ForallElimination&, const ForallElimination&) = default;
};

// A premise the proof may use, counted outwards from the most recently
// introduced one. Hypotheses have their own indices: they are proof-level
// bindings and never denote a term variable.
struct HypothesisIndex {
    std::uint32_t value = 0;

    friend bool operator==(const HypothesisIndex&, const HypothesisIndex&) = default;
};

// Use of a premise standing in the proof context (GRAMMAR.md 5.4).
//
// A hypothesis is not an assumption the kernel grants: it exists only because
// an enclosing implication introduction put it there, and the kernel checks
// that the premise it names really is the goal.
struct Hypothesis {
    HypothesisIndex index;

    friend bool operator==(const Hypothesis&, const Hypothesis&) = default;
};

// Introduction of an implication: evidence for the conclusion, free to use the
// premise. The premise is restated by the evidence and is checked against the
// goal, exactly as a universal introduction restates its binder.
struct ImplicationIntroduction {
    Box<Proposition> premise;
    Box<ProofTerm> body;

    friend bool operator==(const ImplicationIntroduction&, const ImplicationIntroduction&) =
        default;
};

// Elimination of an implication: evidence for `premise -> conclusion` together
// with evidence for the premise (SPEC.md 7.2).
//
// Like universal elimination, the implication eliminated from is restated so
// that the kernel can check that step itself rather than infer it. Both pieces
// of evidence are checked; the conclusion is then the kernel's own.
struct ImplicationElimination {
    Box<Proposition> implication;
    Box<ProofTerm> evidence;
    Box<ProofTerm> premise;

    friend bool operator==(const ImplicationElimination&, const ImplicationElimination&) = default;
};

struct ProofTerm {
    std::variant<Reflexivity,
                 ForallIntroduction,
                 ForallElimination,
                 Hypothesis,
                 ImplicationIntroduction,
                 ImplicationElimination>
        node;

    static ProofTerm reflexivity() { return ProofTerm{Reflexivity{}}; }

    static ProofTerm hypothesis(HypothesisIndex index) { return ProofTerm{Hypothesis{index}}; }

    static ProofTerm implication_introduction(Proposition premise, ProofTerm body) {
        return ProofTerm{ImplicationIntroduction{Box<Proposition>{std::move(premise)},
                                                 Box<ProofTerm>{std::move(body)}}};
    }

    static ProofTerm implication_elimination(Proposition implication,
                                             ProofTerm evidence,
                                             ProofTerm premise) {
        return ProofTerm{ImplicationElimination{Box<Proposition>{std::move(implication)},
                                                Box<ProofTerm>{std::move(evidence)},
                                                Box<ProofTerm>{std::move(premise)}}};
    }

    static ProofTerm forall_introduction(Type binder, ProofTerm body) {
        return ProofTerm{ForallIntroduction{std::move(binder), Box<ProofTerm>{std::move(body)}}};
    }

    static ProofTerm forall_elimination(Proposition quantified, ProofTerm evidence, Term argument) {
        return ProofTerm{ForallElimination{Box<Proposition>{std::move(quantified)},
                                           Box<ProofTerm>{std::move(evidence)},
                                           std::move(argument)}};
    }

    friend bool operator==(const ProofTerm&, const ProofTerm&) = default;
};

std::string describe(const ProofTerm& proof);

}  // namespace cppl::kernel
