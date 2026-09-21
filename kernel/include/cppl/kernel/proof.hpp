#pragma once

#include "cppl/kernel/box.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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

    friend bool operator==(const ImplicationIntroduction&, const ImplicationIntroduction&) = default;
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

// Introduction of a conjunction: evidence for each side (SPEC.md 7.6).
//
//     p : A      q : B
//     ----------------
//         A /\ B
//
// The goal states both sides, so neither is restated here: the kernel takes
// them from the goal and checks each piece of evidence against its own side.
struct ConjunctionIntroduction {
    Box<ProofTerm> left;
    Box<ProofTerm> right;

    friend bool operator==(const ConjunctionIntroduction&, const ConjunctionIntroduction&) = default;
};

// Elimination of a conjunction: either side of it (SPEC.md 7.6).
//
//     p : A /\ B          p : A /\ B
//     ----------          ----------
//         A                   B
//
// The conjunction eliminated from is restated, as universal and implication
// elimination restate theirs, because the goal names only the side taken and
// the other side cannot be recovered from it. The kernel checks the evidence
// against the restated conjunction and then checks that the chosen side is the
// goal, so a wrong restatement can only fail.
struct ConjunctionElimination {
    Box<Proposition> conjunction;
    Box<ProofTerm> evidence;
    bool right = false;

    friend bool operator==(const ConjunctionElimination&, const ConjunctionElimination&) = default;
};

// Introduction of a disjunction: evidence for one of its sides (SPEC.md 7.8).
//
//     p : A              p : B
//     ------------       ------------
//       A \/ B             A \/ B
//
// The goal states both sides, so neither is restated here: the kernel takes the
// selected side from the goal and checks the evidence against it. Establishing
// one side is the only way in; nothing here decides which side is true.
struct DisjunctionIntroduction {
    Box<ProofTerm> evidence;
    bool right = false;

    friend bool operator==(const DisjunctionIntroduction&, const DisjunctionIntroduction&) = default;
};

// Elimination of a disjunction: the goal, proven under each side (SPEC.md 7.8).
//
//     p : A \/ B      q : A -> C      r : B -> C
//     ------------------------------------------
//                        C
//
// The disjunction eliminated from is restated, as the other eliminations restate
// theirs, because the goal names neither side. The kernel checks the evidence
// against the restatement and then checks each case against its own side of it,
// so a case that covers the wrong side, or a restatement that is not what the
// evidence establishes, can only fail. Nothing here learns which side holds.
struct DisjunctionElimination {
    Box<Proposition> disjunction;
    Box<ProofTerm> evidence;
    Box<ProofTerm> left_case;
    Box<ProofTerm> right_case;

    friend bool operator==(const DisjunctionElimination&, const DisjunctionElimination&) = default;
};

// Elimination of an equality: evidence transported through a proposition
// context (SPEC.md 7.2).
//
//     e : a = b      p : C[b]
//     ------------------------
//           C[a]
//
// `motive` is the context C[-], stated with its hole as the innermost bound
// variable, exactly like the body of a Forall. The kernel fills it itself, by
// the same substitution it uses for universal elimination: it checks `equality`
// against `a = b`, checks `evidence` against C[b], and derives C[a].
//
// Which occurrences of a term a context abstracts is a question about what the
// author meant, so the context is given rather than searched for here. Nothing
// about it is taken on trust: the motive is type-checked with its hole standing
// for a term of `type`, and a context that does not yield the goal is refused.
// A mistaken choice of occurrences can therefore only fail to prove something.
//
// Symmetry needs no rule of its own: it is this one at the context `b = -`,
// whose C[b] is `b = b`.
struct EqualityElimination {
    Type type;
    Term lhs;
    Term rhs;
    Box<Proposition> motive;
    Box<ProofTerm> equality;
    Box<ProofTerm> evidence;

    friend bool operator==(const EqualityElimination&, const EqualityElimination&) = default;
};

// A refutation of a linear integer system (see linear.hpp). Each node narrows
// the constraints standing at it; every leaf must be contradictory.
struct ArithmeticCertificate;

// Nonnegative multiples of constraints standing at this point, by their
// position among them, whose sum has no variable left and a positive constant.
// No integer assignment satisfies them all.
struct FarkasSum {
    std::vector<std::pair<std::uint32_t, Wide>> multipliers;

    friend bool operator==(const FarkasSum&, const FarkasSum&) = default;
};

// Every integer assignment makes an integer linear form at most zero or at
// least one, so both are examined and nothing is assumed.
struct IntegerSplit {
    std::vector<std::pair<std::uint32_t, std::int64_t>> terms;
    std::int64_t constant = 0;
    Box<ArithmeticCertificate> at_most_zero;
    Box<ArithmeticCertificate> at_least_one;

    friend bool operator==(const IntegerSplit&, const IntegerSplit&) = default;
};

// One of the two members of a disjunction of the system holds; both cases are
// examined.
struct DisjunctionCases {
    std::uint32_t disjunction = 0;
    Box<ArithmeticCertificate> first;
    Box<ArithmeticCertificate> second;

    friend bool operator==(const DisjunctionCases&, const DisjunctionCases&) = default;
};

struct ArithmeticCertificate {
    std::variant<FarkasSum, IntegerSplit, DisjunctionCases> node;

    friend bool operator==(const ArithmeticCertificate&, const ArithmeticCertificate&) = default;
};

// A fact an arithmetic step reasons from: a proposition restated together with
// the evidence for it, which the kernel checks like any other.
struct ArithmeticFact {
    Proposition proposition;
    Box<ProofTerm> evidence;

    friend bool operator==(const ArithmeticFact&, const ArithmeticFact&) = default;
};

// Linear arithmetic over machine integers (SPEC.md 7.5).
//
//     p1 : F1  ...  pn : Fn      certificate refutes  F1 /\ ... /\ Fn /\ not G
//     -------------------------------------------------------------------------
//                                    G
//
// The kernel checks every fact's evidence, translates the facts and the
// negated goal into integer linear constraints itself, and checks that the
// certificate refutes them. Nothing about the translation is supplied by the
// producer, and a certificate that does not refute the system is refused.
struct LinearArithmetic {
    std::vector<ArithmeticFact> facts;
    ArithmeticCertificate certificate;

    friend bool operator==(const LinearArithmetic&, const LinearArithmetic&) = default;
};

// Both premises are derived from the condition, never supplied by the producer.
struct ConditionalElimination {
    Type type;
    Term condition;
    Term when_true;
    Term when_false;
    Box<Proposition> motive;
    Box<ProofTerm> true_case;
    Box<ProofTerm> false_case;

    friend bool operator==(const ConditionalElimination&, const ConditionalElimination&) = default;
};

struct ProofTerm {
    std::variant<Reflexivity, ForallIntroduction, ForallElimination, Hypothesis, ImplicationIntroduction,
                 ImplicationElimination, EqualityElimination, ConditionalElimination, LinearArithmetic,
                 ConjunctionIntroduction, ConjunctionElimination, DisjunctionIntroduction, DisjunctionElimination>
        node;

    static ProofTerm reflexivity() {
        return ProofTerm{Reflexivity{}};
    }

    static ProofTerm linear_arithmetic(std::vector<ArithmeticFact> facts, ArithmeticCertificate certificate) {
        return ProofTerm{LinearArithmetic{std::move(facts), std::move(certificate)}};
    }

    static ProofTerm conditional_elimination(Type type, Term condition, Term when_true, Term when_false,
                                             Proposition motive, ProofTerm true_case, ProofTerm false_case) {
        return ProofTerm{ConditionalElimination{std::move(type), std::move(condition), std::move(when_true),
                                                std::move(when_false), Box<Proposition>{std::move(motive)},
                                                Box<ProofTerm>{std::move(true_case)},
                                                Box<ProofTerm>{std::move(false_case)}}};
    }

    static ProofTerm hypothesis(HypothesisIndex index) {
        return ProofTerm{Hypothesis{index}};
    }

    static ProofTerm implication_introduction(Proposition premise, ProofTerm body) {
        return ProofTerm{
            ImplicationIntroduction{Box<Proposition>{std::move(premise)}, Box<ProofTerm>{std::move(body)}}};
    }

    static ProofTerm implication_elimination(Proposition implication, ProofTerm evidence, ProofTerm premise) {
        return ProofTerm{ImplicationElimination{Box<Proposition>{std::move(implication)},
                                                Box<ProofTerm>{std::move(evidence)},
                                                Box<ProofTerm>{std::move(premise)}}};
    }

    static ProofTerm equality_elimination(Type type, Term lhs, Term rhs, Proposition motive, ProofTerm equality,
                                          ProofTerm evidence) {
        return ProofTerm{EqualityElimination{std::move(type), std::move(lhs), std::move(rhs),
                                             Box<Proposition>{std::move(motive)}, Box<ProofTerm>{std::move(equality)},
                                             Box<ProofTerm>{std::move(evidence)}}};
    }

    static ProofTerm forall_introduction(Type binder, ProofTerm body) {
        return ProofTerm{ForallIntroduction{std::move(binder), Box<ProofTerm>{std::move(body)}}};
    }

    static ProofTerm conjunction_introduction(ProofTerm left, ProofTerm right) {
        return ProofTerm{ConjunctionIntroduction{Box<ProofTerm>{std::move(left)}, Box<ProofTerm>{std::move(right)}}};
    }

    static ProofTerm conjunction_elimination(Proposition conjunction, ProofTerm evidence, bool right) {
        return ProofTerm{ConjunctionElimination{Box<Proposition>{std::move(conjunction)},
                                                Box<ProofTerm>{std::move(evidence)}, right}};
    }

    static ProofTerm disjunction_introduction(ProofTerm evidence, bool right) {
        return ProofTerm{DisjunctionIntroduction{Box<ProofTerm>{std::move(evidence)}, right}};
    }

    static ProofTerm disjunction_elimination(Proposition disjunction, ProofTerm evidence, ProofTerm left_case,
                                             ProofTerm right_case) {
        return ProofTerm{
            DisjunctionElimination{Box<Proposition>{std::move(disjunction)}, Box<ProofTerm>{std::move(evidence)},
                                   Box<ProofTerm>{std::move(left_case)}, Box<ProofTerm>{std::move(right_case)}}};
    }

    static ProofTerm forall_elimination(Proposition quantified, ProofTerm evidence, Term argument) {
        return ProofTerm{ForallElimination{Box<Proposition>{std::move(quantified)}, Box<ProofTerm>{std::move(evidence)},
                                           std::move(argument)}};
    }

    friend bool operator==(const ProofTerm&, const ProofTerm&) = default;
};

std::string describe(const ProofTerm& proof);

} // namespace cppl::kernel
