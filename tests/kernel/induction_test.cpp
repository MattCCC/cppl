// Unsigned induction, the kernel's fifteenth rule (FOUNDATIONS.md 74, SPEC.md
// INDUCT-002, INDUCT-003, INDUCT-004, INTERACT-026).
//
//     base : P(0)      step : forall n : T. n < max(T) -> P(n) -> P(n + 1)
//     ---------------------------------------------------------------------
//                           forall n : T. P(n)
//
// Every test calls the real kernel API and builds its evidence by hand: nothing
// here involves the frontend, elaboration or automation. The rule is exercised
// with valid evidence that must be accepted, evidence that must be refused and
// malformed evidence, and a property test sets random motives against a brute
// force evaluation of every value of a small type.

#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace k = cppl::kernel;

const k::IntType kU8{8, k::Signedness::Unsigned};
const k::IntType kI8{8, k::Signedness::Signed};
const k::IntType kU64{64, k::Signedness::Unsigned};

k::Type u8() {
    return k::Type{kU8};
}

k::Term var(std::uint32_t index) {
    return k::Term::variable(k::VarIndex{index});
}

k::Term lit(const k::IntType& type, k::Wide value) {
    return k::Term::literal(type, value);
}

k::Term add(const k::IntType& type, k::Term lhs, k::Term rhs) {
    return k::Term::primitive(k::PrimOp::AddWrap, type, {std::move(lhs), std::move(rhs)});
}

k::Proposition eq(const k::IntType& type, k::Term lhs, k::Term rhs) {
    return k::Proposition::equality(k::Type{type}, std::move(lhs), std::move(rhs));
}

k::Proposition below(const k::IntType& type, k::Term lhs, k::Term rhs) {
    return k::predicate(k::Term::primitive(k::PrimOp::Less, type, {std::move(lhs), std::move(rhs)}), true);
}

k::ProofTerm hyp(std::uint32_t index) {
    return k::ProofTerm::hypothesis(k::HypothesisIndex{index});
}

k::ProofTerm refl() {
    return k::ProofTerm::reflexivity();
}

// The step's shape for a step closed by `conclusion`: introduce `n`, the range
// premise and the hypothesis, in the order the kernel states them.
k::ProofTerm step(const k::IntType& type, const k::Proposition& body, k::ProofTerm conclusion) {
    return k::ProofTerm::forall_introduction(
        k::Type{type},
        k::ProofTerm::implication_introduction(below(type, var(0), lit(type, k::maximum_value(type))),
                                               k::ProofTerm::implication_introduction(body, std::move(conclusion))));
}

bool accepted(const k::Proposition& goal, const k::ProofTerm& proof, const k::Context& context = {}) {
    return k::check(context, goal, proof, k::CoreLimits{}).has_value();
}

std::optional<k::RejectionKind> rejection(const k::Proposition& goal, const k::ProofTerm& proof) {
    const auto result = k::check({}, goal, proof, k::CoreLimits{});
    if (result.has_value())
        return std::nullopt;
    return result.error().kind;
}

// An array of 256 bytes observed at a term: `a[i]`. It is the one kind of
// value in this core whose observation at an arbitrary index is not decided by
// normalization, so a claim about all of its elements needs the hypothesis.
const k::Type kBytes = k::Type::indexed(u8(), 256);

k::Term at(k::Term array, k::Term index) {
    return k::Term::element(kBytes, std::move(array), std::move(index));
}

// forall a. a[0] == 0 -> (forall m. a[m] == 0 -> a[m + 1] == 0) -> forall n. a[n] == 0
//
// The conclusion follows from the two premises only by induction: nothing in
// them relates `a[n]` to `a[0]` for any particular `n`.
k::Proposition zero_filled_premise_base() {
    return eq(kU8, at(var(0), lit(kU8, 0)), lit(kU8, 0));
}
k::Proposition zero_filled_premise_step() {
    return k::Proposition::for_all(
        u8(), k::Proposition::implication(eq(kU8, at(var(1), var(0)), lit(kU8, 0)),
                                          eq(kU8, at(var(1), add(kU8, var(0), lit(kU8, 1))), lit(kU8, 0))));
}
k::Proposition zero_filled_body() {
    return eq(kU8, at(var(1), var(0)), lit(kU8, 0));
}
k::Proposition zero_filled() {
    return k::Proposition::for_all(
        kBytes,
        k::Proposition::implication(zero_filled_premise_base(),
                                    k::Proposition::implication(zero_filled_premise_step(),
                                                                k::Proposition::for_all(u8(), zero_filled_body()))));
}

// The successor step for zero_filled: instantiate the step premise at `n` and
// discharge it with the hypothesis. Hypotheses, innermost first: the induction
// hypothesis 0, the range premise 1, the step premise 2, the base premise 3.
k::ProofTerm zero_filled_step_conclusion(std::uint32_t hypothesis) {
    const k::Proposition premise = k::shift(zero_filled_premise_step(), 1);
    const k::Proposition instance = k::instantiate(std::get<k::Forall>(premise.node).body.get(), var(0));
    return k::ProofTerm::implication_elimination(instance, k::ProofTerm::forall_elimination(premise, hyp(2), var(0)),
                                                 hyp(hypothesis));
}

k::ProofTerm zero_filled_evidence(k::ProofTerm base, k::ProofTerm successor) {
    return k::ProofTerm::forall_introduction(
        kBytes, k::ProofTerm::implication_introduction(
                    zero_filled_premise_base(),
                    k::ProofTerm::implication_introduction(
                        zero_filled_premise_step(),
                        k::ProofTerm::unsigned_induction(u8(), std::move(base), std::move(successor)))));
}

} // namespace

// SPEC: INDUCT-002, INDUCT-003
CPPL_TEST(induction_proves_a_claim_only_the_hypothesis_reaches) {
    const auto evidence = zero_filled_evidence(hyp(1), step(kU8, zero_filled_body(), zero_filled_step_conclusion(0)));
    CPPL_CHECK(accepted(zero_filled(), evidence));
}

// SPEC: INDUCT-002
// The same premises with the conclusion stated by a universal introduction
// instead: without the principle nothing relates a[n] to a[0].
CPPL_TEST(without_induction_the_same_claim_is_not_established) {
    const auto introduced = k::ProofTerm::forall_introduction(
        kBytes, k::ProofTerm::implication_introduction(
                    zero_filled_premise_base(),
                    k::ProofTerm::implication_introduction(zero_filled_premise_step(),
                                                           k::ProofTerm::forall_introduction(u8(), hyp(1)))));
    CPPL_CHECK(!accepted(zero_filled(), introduced));
}

// SPEC: INDUCT-003
// The premises are the kernel's own: P(0), and P(n) -> P(n + 1) under n < max.
// Stated here by hand for a motive with a free outer variable, so a slip in
// the lifting or substitution the kernel performs shows up as a difference.
CPPL_TEST(the_kernel_states_both_premises_itself) {
    // P(n) := a[n] == a[n + 1], under one outer binder a.
    const k::Proposition body = eq(kU8, at(var(1), var(0)), at(var(1), add(kU8, var(0), lit(kU8, 1))));
    const k::Proposition base = eq(kU8, at(var(0), lit(kU8, 0)), at(var(0), add(kU8, lit(kU8, 0), lit(kU8, 1))));
    CPPL_CHECK(k::induction_base(kU8, body) == base);

    const k::Proposition expected_step = k::Proposition::for_all(
        u8(),
        k::Proposition::implication(
            below(kU8, var(0), lit(kU8, 255)),
            k::Proposition::implication(body, eq(kU8, at(var(1), add(kU8, var(0), lit(kU8, 1))),
                                                 at(var(1), add(kU8, add(kU8, var(0), lit(kU8, 1)), lit(kU8, 1)))))));
    CPPL_CHECK(k::induction_step(kU8, body) == expected_step);
}

// SPEC: INDUCT-003, INTERACT-026
// The range premise is stated at the type's own maximum, 2^64 - 1 for a 64-bit
// type, which no signed 64-bit integer holds.
CPPL_TEST(the_range_premise_is_the_types_own_maximum) {
    const k::Proposition body = eq(kU64, var(0), var(0));
    const k::Proposition stated = k::induction_step(kU64, body);
    const auto& quantified = std::get<k::Forall>(stated.node);
    const auto& range = std::get<k::Implies>(quantified.body->node).premise.get();
    const k::Wide maximum = (k::Wide{1} << 64) - 1;
    CPPL_CHECK(range == below(kU64, var(0), lit(kU64, maximum)));
}

// SPEC: INDUCT-003
// The range premise is supplied to the step and can be used there. P(x) is
// `(x - 1 < 255) || x == 0`; the successor case's left side, (n + 1) - 1 < 255,
// is the range premise up to definitional equality, so the step converts the
// premise into it and names nothing else. The same step naming the hypothesis
// in its place is refused.
CPPL_TEST(the_step_may_use_the_range_premise) {
    const auto sub = [](k::Term lhs, k::Term rhs) {
        return k::Term::primitive(k::PrimOp::SubWrap, kU8, {std::move(lhs), std::move(rhs)});
    };
    const k::Proposition body =
        k::Proposition::disjunction(below(kU8, sub(var(0), lit(kU8, 1)), lit(kU8, 255)), eq(kU8, var(0), lit(kU8, 0)));
    const k::Proposition goal = k::Proposition::for_all(u8(), body);
    const k::Term stated = k::Term::primitive(k::PrimOp::Less, kU8, {var(0), lit(kU8, 255)});
    const k::Term successor =
        k::Term::primitive(k::PrimOp::Less, kU8, {sub(add(kU8, var(0), lit(kU8, 1)), lit(kU8, 1)), lit(kU8, 255)});
    const auto converted = [&](std::uint32_t premise) {
        return k::ProofTerm::disjunction_introduction(
            k::ProofTerm::equality_elimination(k::Type{k::kBoolean}, successor, stated,
                                               eq(k::kBoolean, var(0), lit(k::kBoolean, 1)), refl(), hyp(premise)),
            false);
    };
    const auto base = k::ProofTerm::disjunction_introduction(refl(), true);
    CPPL_CHECK(accepted(goal, k::ProofTerm::unsigned_induction(u8(), base, step(kU8, body, converted(1)))));
    CPPL_CHECK(!accepted(goal, k::ProofTerm::unsigned_induction(u8(), base, step(kU8, body, converted(0)))));
}

// SPEC: INDUCT-004
// A true claim over a signed type is still refused: signed values are not
// generated from zero by successor, so the rule has no principle for them.
CPPL_TEST(a_signed_type_has_no_principle_even_for_a_true_claim) {
    const k::Proposition body = eq(kI8, var(0), var(0));
    const k::Proposition goal = k::Proposition::for_all(k::Type{kI8}, body);
    const auto evidence = k::ProofTerm::unsigned_induction(
        k::Type{kI8}, refl(),
        k::ProofTerm::forall_introduction(k::Type{kI8}, k::ProofTerm::implication_introduction(
                                                            below(kI8, var(0), lit(kI8, 127)),
                                                            k::ProofTerm::implication_introduction(body, refl()))));
    CPPL_CHECK(rejection(goal, evidence) == k::RejectionKind::ProofShapeMismatch);
    // The same claim by universal introduction is accepted: the refusal is the
    // rule's, not the claim's.
    CPPL_CHECK(accepted(goal, k::ProofTerm::forall_introduction(k::Type{kI8}, refl())));
}

// SPEC: INDUCT-004
// An abstract value and an indexed domain have no successor either.
CPPL_TEST(abstract_and_indexed_types_have_no_principle) {
    const k::Type object = k::Type::value("object", {u8()});
    for (const k::Type& binder : {object, kBytes}) {
        const k::Proposition body = eq(kU8, lit(kU8, 1), lit(kU8, 1));
        const k::Proposition goal = k::Proposition::for_all(binder, body);
        const auto evidence = k::ProofTerm::unsigned_induction(binder, refl(), refl());
        CPPL_CHECK(rejection(goal, evidence) == k::RejectionKind::ProofShapeMismatch);
    }
}

// The evidence restates its binder, and it must be the goal's.
CPPL_TEST(induction_over_another_type_is_refused) {
    const k::Proposition goal = k::Proposition::for_all(u8(), eq(kU8, var(0), var(0)));
    const k::IntType u16{16, k::Signedness::Unsigned};
    const auto evidence =
        k::ProofTerm::unsigned_induction(k::Type{u16}, refl(), step(u16, eq(u16, var(0), var(0)), refl()));
    CPPL_CHECK(rejection(goal, evidence) == k::RejectionKind::ProofShapeMismatch);
}

// Induction establishes a quantified goal and nothing else.
CPPL_TEST(induction_is_refused_for_a_goal_that_quantifies_over_nothing) {
    const k::Proposition goal = eq(kU8, lit(kU8, 3), lit(kU8, 3));
    CPPL_CHECK(rejection(goal, k::ProofTerm::unsigned_induction(u8(), refl(), refl())) ==
               k::RejectionKind::ProofShapeMismatch);
}

// SPEC: INDUCT-005
// Every case must prove the goal: a wrong base case fails, whatever the step.
CPPL_TEST(a_base_case_that_does_not_hold_at_zero_is_refused) {
    // P(n) := n + 1 == 0 holds nowhere reachable from zero... except at 255.
    const k::Proposition body = eq(kU8, add(kU8, var(0), lit(kU8, 1)), lit(kU8, 0));
    const k::Proposition goal = k::Proposition::for_all(u8(), body);
    CPPL_CHECK(!accepted(goal, k::ProofTerm::unsigned_induction(u8(), refl(), step(kU8, body, hyp(0)))));
}

// SPEC: INDUCT-003, INTERACT-026
// The hypothesis is P(n), never P(n + 1): offering it as the successor's
// evidence proves nothing unless P does not depend on n.
CPPL_TEST(the_hypothesis_is_not_the_successor_case) {
    // P(n) := a[n] == 0 with no step premise: base from the premise, step from
    // the hypothesis alone.
    const k::Proposition goal =
        k::Proposition::for_all(kBytes, k::Proposition::implication(zero_filled_premise_base(),
                                                                    k::Proposition::for_all(u8(), zero_filled_body())));
    const auto evidence = k::ProofTerm::forall_introduction(
        kBytes, k::ProofTerm::implication_introduction(
                    zero_filled_premise_base(),
                    k::ProofTerm::unsigned_induction(u8(), hyp(0), step(kU8, zero_filled_body(), hyp(0)))));
    CPPL_CHECK(rejection(goal, evidence) == k::RejectionKind::ProofShapeMismatch);
}

// FOUNDATIONS.md 10: an induction hypothesis is local to its case. The base
// case has none, so naming one there names nothing, or names something else.
CPPL_TEST(the_hypothesis_does_not_reach_the_base_case) {
    // In the base case only the two premises stand; index 2 is out of scope.
    const auto evidence = zero_filled_evidence(hyp(2), step(kU8, zero_filled_body(), zero_filled_step_conclusion(0)));
    CPPL_CHECK(rejection(zero_filled(), evidence) == k::RejectionKind::MalformedProofTerm);
}

// The hypothesis is the innermost premise of the step; the range premise is
// not it, and naming the range premise where the hypothesis is needed fails.
CPPL_TEST(the_range_premise_is_not_the_hypothesis) {
    const auto evidence = zero_filled_evidence(hyp(1), step(kU8, zero_filled_body(), zero_filled_step_conclusion(1)));
    CPPL_CHECK(rejection(zero_filled(), evidence) == k::RejectionKind::ProofShapeMismatch);
}

// AGENTS.md 38: induction cannot prove False, nor a claim false at one value.
CPPL_TEST(induction_cannot_prove_false_things) {
    // forall n. False
    const k::Proposition never = k::Proposition::for_all(u8(), k::Proposition::falsity());
    CPPL_CHECK(
        !accepted(never, k::ProofTerm::unsigned_induction(u8(), hyp(0), step(kU8, k::Proposition::falsity(), hyp(0)))));
    CPPL_CHECK(!accepted(never, k::ProofTerm::unsigned_induction(
                                    u8(), k::ProofTerm::falsity_elimination(hyp(0)),
                                    step(kU8, k::Proposition::falsity(), k::ProofTerm::falsity_elimination(hyp(0))))));
    // forall n. n != 255, true at 0 and preserved by every step that stays in
    // range except the last: only wrapping 255 + 1 to 0 would close it.
    const k::Proposition not_maximum =
        k::predicate(k::Term::primitive(k::PrimOp::NotEqual, kU8, {var(0), lit(kU8, 255)}), true);
    const k::Proposition claim = k::Proposition::for_all(u8(), not_maximum);
    CPPL_CHECK(!accepted(claim, k::ProofTerm::unsigned_induction(u8(), refl(), step(kU8, not_maximum, hyp(0)))));
    CPPL_CHECK(!accepted(claim, k::ProofTerm::unsigned_induction(u8(), refl(), step(kU8, not_maximum, refl()))));
}

// Malformed evidence fails closed wherever it stands.
CPPL_TEST(malformed_induction_evidence_is_refused) {
    const k::Proposition goal = k::Proposition::for_all(u8(), eq(kU8, var(0), var(0)));
    // An unsupported binder type.
    CPPL_CHECK(!accepted(goal, k::ProofTerm::unsigned_induction(k::Type::integer(0, k::Signedness::Unsigned), refl(),
                                                                step(kU8, eq(kU8, var(0), var(0)), refl()))));
    // A step that is not stated as a quantifier over the type.
    CPPL_CHECK(!accepted(goal, k::ProofTerm::unsigned_induction(u8(), refl(), refl())));
    // A step that skips the range premise.
    CPPL_CHECK(!accepted(goal, k::ProofTerm::unsigned_induction(
                                   u8(), refl(),
                                   k::ProofTerm::forall_introduction(u8(), k::ProofTerm::implication_introduction(
                                                                               eq(kU8, var(0), var(0)), refl())))));
    // A step that states a weaker range premise than the kernel does.
    CPPL_CHECK(!accepted(goal, k::ProofTerm::unsigned_induction(u8(), refl(),
                                                                k::ProofTerm::forall_introduction(
                                                                    u8(), k::ProofTerm::implication_introduction(
                                                                              below(kU8, var(0), lit(kU8, 254)),
                                                                              k::ProofTerm::implication_introduction(
                                                                                  eq(kU8, var(0), var(0)), refl()))))));
    // A hypothesis index past everything in scope.
    CPPL_CHECK(
        !accepted(goal, k::ProofTerm::unsigned_induction(u8(), hyp(7), step(kU8, eq(kU8, var(0), var(0)), hyp(9)))));
    // Nested inductions are ordinary evidence and are checked as such.
    const k::Proposition two = k::Proposition::for_all(u8(), k::Proposition::for_all(u8(), eq(kU8, var(1), var(1))));
    const auto inner_body = eq(kU8, var(1), var(1));
    const auto inner = k::ProofTerm::unsigned_induction(u8(), refl(), step(kU8, inner_body, refl()));
    CPPL_CHECK(accepted(two, k::ProofTerm::forall_introduction(u8(), inner)));
    const auto outer_body = k::Proposition::for_all(u8(), eq(kU8, var(1), var(1)));
    CPPL_CHECK(accepted(
        two, k::ProofTerm::unsigned_induction(u8(), k::ProofTerm::forall_introduction(u8(), refl()),
                                              step(kU8, outer_body, k::ProofTerm::forall_introduction(u8(), refl())))));
}

namespace {

// An evaluator independent of the kernel's normalizer, for the property test:
// two's-complement arithmetic on the few primitives the motives below use.
struct Evaluator {
    std::uint32_t width = 3;

    [[nodiscard]] std::uint64_t mask() const {
        return (std::uint64_t{1} << width) - 1;
    }

    [[nodiscard]] std::optional<std::uint64_t> term(const k::Term& t, std::uint64_t n) const {
        if (const auto* variable = std::get_if<k::Var>(&t.node))
            return variable->index.value == 0 ? std::optional<std::uint64_t>{n} : std::nullopt;
        if (const auto* literal = std::get_if<k::Literal>(&t.node))
            return static_cast<std::uint64_t>(literal->value) & mask();
        const auto* primitive = std::get_if<k::Prim>(&t.node);
        if (primitive == nullptr || primitive->arguments.size() != 2)
            return std::nullopt;
        const auto a = term(primitive->arguments[0], n);
        const auto b = term(primitive->arguments[1], n);
        if (!a || !b)
            return std::nullopt;
        switch (primitive->op) {
            case k::PrimOp::AddWrap:
                return (*a + *b) & mask();
            case k::PrimOp::MulWrap:
                return (*a * *b) & mask();
            case k::PrimOp::Less:
                return *a < *b ? 1 : 0;
            default:
                return std::nullopt;
        }
    }

    // P(n) for an equality motive.
    [[nodiscard]] std::optional<bool> holds(const k::Proposition& p, std::uint64_t n) const {
        const auto* equality = std::get_if<k::Eq>(&p.node);
        if (equality == nullptr)
            return std::nullopt;
        const auto a = term(equality->lhs, n);
        const auto b = term(equality->rhs, n);
        if (!a || !b)
            return std::nullopt;
        return *a == *b;
    }
};

struct Random {
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;
    std::uint32_t below(std::uint32_t n) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<std::uint32_t>(state % n);
    }
};

k::Term random_polynomial(Random& random, const k::IntType& type, unsigned depth) {
    const auto choice = random.below(depth == 0 ? 2 : 4);
    if (choice == 0)
        return var(0);
    if (choice == 1)
        return lit(type, random.below(8) & 7u);
    const auto op = choice == 2 ? k::PrimOp::AddWrap : k::PrimOp::MulWrap;
    return k::Term::primitive(op, type,
                              {random_polynomial(random, type, depth - 1), random_polynomial(random, type, depth - 1)});
}

} // namespace

// SPEC: INDUCT-002, INDUCT-003
// For random motives over a 3-bit type, every candidate the kernel accepts
// holds at all eight values, by an evaluation that shares nothing with the
// kernel. The candidates include the step a substitution slip would make
// valid, the hypothesis offered as the successor case, so a kernel that stated
// P(n) -> P(n) accepts motives false at some value and fails here. Acceptances
// must occur, so the property cannot pass vacuously.
CPPL_TEST(accepted_inductions_hold_at_every_value) {
    const k::IntType type{3, k::Signedness::Unsigned};
    const Evaluator evaluator{3};
    Random random;
    std::size_t acceptances = 0;
    std::size_t false_motives = 0;
    for (unsigned sample = 0; sample < 3000; ++sample) {
        const k::Proposition body =
            random.below(3) == 0 ? k::predicate(k::Term::primitive(k::PrimOp::Less, type,
                                                                   {random_polynomial(random, type, 2),
                                                                    random_polynomial(random, type, 2)}),
                                                true)
                                 : eq(type, random_polynomial(random, type, 2), random_polynomial(random, type, 2));
        bool everywhere = true;
        bool evaluated = true;
        for (std::uint64_t n = 0; n < 8; ++n) {
            const auto value = evaluator.holds(body, n);
            if (!value) {
                evaluated = false;
                break;
            }
            everywhere = everywhere && *value;
        }
        if (!evaluated)
            continue;
        false_motives += everywhere ? 0 : 1;
        const k::Proposition goal = k::Proposition::for_all(k::Type{type}, body);
        for (const k::ProofTerm& successor :
             {step(type, body, refl()), step(type, body, hyp(0)), step(type, body, hyp(1))}) {
            if (accepted(goal, k::ProofTerm::unsigned_induction(k::Type{type}, refl(), successor))) {
                ++acceptances;
                CPPL_CHECK(everywhere);
            }
        }
    }
    CPPL_CHECK(acceptances > 100);
    CPPL_CHECK(false_motives > 100);
}
