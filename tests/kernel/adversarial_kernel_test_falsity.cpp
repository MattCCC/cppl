// Adversarial kernel tests (kernel_adversarial_test): discharging a
// contradictory branch, and falsity elimination. Every attack here
// MUST fail if the kernel is sound.

#include "adversarial_kernel_test_support.hpp"
#include "cppl/kernel/box.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/linear.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

using namespace adversarial_kernel_test_detail;

// ============================================================================
// EX FALSO: discharging a branch whose premises are contradictory
// ============================================================================
//
// Omitting a case (SPEC.md CASE-004/005) and discharging an impossible path
// (VERIFIED-023) both need one operation: from checked contradiction evidence
// in the current context, close a goal of any shape. The contradiction is
// evidence for `False`, obtained by linear arithmetic refuting the facts alone,
// and the goal is closed from it by falsity elimination (FOUNDATIONS.md 26),
// whose own tests follow this section.
//
// Transport along an absurd equality is NOT the mechanism, and these tests pin
// why. Equality elimination needs evidence of the goal at the equality's other
// side, so with a constant motive it needs the goal to prove the goal: it can
// conclude the contradiction itself and nothing else. Linear arithmetic alone
// reaches further - a fact no value satisfies refutes any system it stands in,
// whatever the goal is - but only for goals it can state, which are equalities
// of integers. Neither reaches an equality of structured values, which is why
// falsity elimination is a rule of its own.

CPPL_TEST(a_contradictory_premise_closes_any_goal) {
    // Transport along an absurd premise, the derivation an omitted case might
    // have been expected to use and does not. `0 = 1` is introduced as a
    // hypothesis so the test is about the elimination alone.
    cppl::kernel::Context ctx;

    // An arbitrary goal, unrelated to the premise: `7 = 7` is provable, so it
    // proves nothing on its own. The point is the shape below.
    const auto absurd = eq(0, 1);

    // Motive `h. 7 = h`, transported along `0 = 1`: evidence at `0` becomes
    // evidence at `1`, so `7 = 0` would yield `7 = 1`.
    const auto motive = Proposition::equality(u32(), literal(7), var0());
    const auto goal = Proposition::implication(absurd, Proposition::equality(u32(), literal(7), literal(1)));

    // Inside, hypothesis 0 is `0 = 1`. Transport `7 = 0` along it. `7 = 0` is
    // itself false, so this alone must NOT check: a contradiction lets you
    // conclude anything only once you have actually derived the contradiction,
    // not merely assumed a premise and ignored it.
    const auto body = ProofTerm::equality_elimination(u32(), literal(0), literal(1), motive, ProofTerm::hypothesis({0}),
                                                      ProofTerm::reflexivity());
    const auto attempt = ProofTerm::implication_introduction(absurd, body);
    const auto result = cppl::kernel::check(ctx, goal, attempt, {});

    // Rejected: the transported evidence `7 = 0` was closed by reflexivity,
    // and 7 does not reduce to 0. The premise being absurd does not excuse
    // supplying false evidence under it.
    CPPL_CHECK(!result.has_value());
}

CPPL_TEST(transport_along_a_contradiction_is_the_ex_falso_shape) {
    // The same elimination where it does work: the goal's own term is what gets
    // transported, so it closes without proving anything about the subject. It
    // works only because the goal is the contradiction itself.
    cppl::kernel::Context ctx;

    // Premise `0 = 1`. Motive `h. h = 1`. At `1` the goal is `1 = 1`, closed
    // by reflexivity; transporting back along the premise yields `0 = 1`,
    // which is the goal under this (contradictory) premise.
    const auto absurd = eq(0, 1);
    const auto motive = Proposition::equality(u32(), var0(), literal(1));
    const auto evidence = ProofTerm::equality_elimination(u32(), literal(0), literal(1), motive,
                                                          ProofTerm::hypothesis({0}), ProofTerm::reflexivity());
    const auto goal = Proposition::implication(absurd, absurd);
    const auto result = cppl::kernel::check(ctx, goal, ProofTerm::implication_introduction(absurd, evidence), {});

    // Accepted: every step is an existing rule, and the conclusion is only
    // reached under the absurd premise. Note what this does NOT show: the goal
    // here is the contradiction itself, so transport closes it only because the
    // goal's own terms are the ones being transported. An unrelated goal is not
    // reachable this way, which is why a discharged case goes through `False`
    // instead.
    CPPL_CHECK(result.has_value());
}

CPPL_TEST(a_case_cannot_be_omitted_without_evidence_of_its_impossibility) {
    // The attack CASE-005 names: claim a case is impossible and close its
    // branch, supplying no contradiction at all. Modeled here as closing an
    // arbitrary goal with no premise in scope.
    cppl::kernel::Context ctx;
    const auto goal = eq(0, 1);
    const auto result = cppl::kernel::check(ctx, goal, ProofTerm::reflexivity(), {});
    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::NotDefinitionallyEqual);
}

CPPL_TEST(contradictory_facts_close_an_unrelated_goal_through_arithmetic) {
    // Linear arithmetic's own reach. The fact `0 = 1` is contradictory on its
    // own, and the goal `5 = 9` is unrelated to it and false, so nothing about
    // the goal can be closing it.
    //
    // The kernel states the constraints from the facts and the negated goal
    // itself. A fact no value satisfies leaves a constraint with no variable
    // and a positive constant, which refutes the system whatever the goal
    // contributed, so a certificate naming that one constraint is enough.
    cppl::kernel::Context ctx;
    const auto absurd = eq(0, 1);
    const auto unrelated = Proposition::equality(u32(), literal(5), literal(9));

    const Proposition facts[] = {absurd};
    const auto system = cppl::kernel::arithmetic_system(ctx, facts, unrelated, cppl::kernel::CoreLimits{});
    CPPL_CHECK(system.has_value());

    const auto contradictory = std::ranges::find_if(
        system->constraints, [](const auto& entry) { return entry.terms.empty() && entry.constant > 0; });
    CPPL_CHECK(contradictory != system->constraints.end());

    cppl::kernel::FarkasSum refutation;
    refutation.multipliers.emplace_back(
        static_cast<std::uint32_t>(std::distance(system->constraints.begin(), contradictory)), cppl::kernel::Wide{1});

    std::vector<cppl::kernel::ArithmeticFact> stated;
    stated.push_back(cppl::kernel::ArithmeticFact{absurd, cppl::kernel::Box<ProofTerm>{ProofTerm::hypothesis({0})}});
    const auto evidence =
        ProofTerm::linear_arithmetic(std::move(stated), cppl::kernel::ArithmeticCertificate{refutation});
    const auto goal = Proposition::implication(absurd, unrelated);
    const auto result = cppl::kernel::check(ctx, goal, ProofTerm::implication_introduction(absurd, evidence), {});

    // Accepted, and only under the absurd premise: the same term without the
    // premise in scope has no hypothesis to check its fact against.
    CPPL_CHECK(result.has_value());
    CPPL_CHECK(!cppl::kernel::check(ctx, unrelated, evidence, {}).has_value());
}

CPPL_TEST(a_satisfiable_fact_closes_nothing) {
    // The soundness direction of the rule above. `0 = 0` is true, so it is not
    // a contradiction, and the system it states with the negated goal has a
    // solution. No certificate over it can refute anything.
    cppl::kernel::Context ctx;
    const auto satisfiable = eq(0, 0);
    const auto unrelated = Proposition::equality(u32(), literal(5), literal(9));

    const Proposition facts[] = {satisfiable};
    const auto system = cppl::kernel::arithmetic_system(ctx, facts, unrelated, cppl::kernel::CoreLimits{});
    CPPL_CHECK(system.has_value());

    // No constraint is contradictory on its own.
    const auto contradictory = std::ranges::find_if(
        system->constraints, [](const auto& entry) { return entry.terms.empty() && entry.constant > 0; });
    CPPL_CHECK(contradictory == system->constraints.end());

    // Proposing one anyway is refused: every single-constraint sum fails.
    for (std::size_t index = 0; index < system->constraints.size(); ++index) {
        cppl::kernel::FarkasSum forged;
        forged.multipliers.emplace_back(static_cast<std::uint32_t>(index), cppl::kernel::Wide{1});
        std::vector<cppl::kernel::ArithmeticFact> stated;
        stated.push_back(
            cppl::kernel::ArithmeticFact{satisfiable, cppl::kernel::Box<ProofTerm>{ProofTerm::hypothesis({0})}});
        const auto evidence =
            ProofTerm::linear_arithmetic(std::move(stated), cppl::kernel::ArithmeticCertificate{forged});
        const auto goal = Proposition::implication(satisfiable, unrelated);
        CPPL_CHECK(!cppl::kernel::check(ctx, goal, ProofTerm::implication_introduction(satisfiable, evidence), {}));
    }
}

CPPL_TEST(a_hypothesis_that_is_not_in_scope_cannot_supply_the_contradiction) {
    // Naming a premise that no introduction placed in the context is the other
    // way to fake an impossibility. The kernel holds the context itself.
    cppl::kernel::Context ctx;
    const auto motive = Proposition::equality(u32(), var0(), literal(1));
    const auto forged = ProofTerm::equality_elimination(u32(), literal(0), literal(1), motive,
                                                        ProofTerm::hypothesis({0}), ProofTerm::reflexivity());
    const auto result = cppl::kernel::check(ctx, eq(0, 1), forged, {});
    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::MalformedProofTerm);
}

namespace {

const Type kPair = Type::value("pair", {u32(), u32()});

// Evidence of `False` from the fact `0 = 1`, which stands as hypothesis `index`:
// linear arithmetic refuting that fact alone.
ProofTerm refutation_of_zero_is_one(const cppl::kernel::Context& ctx, std::uint32_t index) {
    const Proposition facts[] = {eq(0, 1)};
    const auto system = cppl::kernel::arithmetic_system(ctx, facts, Proposition::falsity(), CoreLimits{});
    CPPL_CHECK(system.has_value());
    const auto contradictory = std::ranges::find_if(
        system->constraints, [](const auto& entry) { return entry.terms.empty() && entry.constant > 0; });
    CPPL_CHECK(contradictory != system->constraints.end());
    cppl::kernel::FarkasSum refutation;
    refutation.multipliers.emplace_back(
        static_cast<std::uint32_t>(std::distance(system->constraints.begin(), contradictory)), cppl::kernel::Wide{1});
    std::vector<cppl::kernel::ArithmeticFact> stated;
    stated.push_back(
        cppl::kernel::ArithmeticFact{eq(0, 1), cppl::kernel::Box<ProofTerm>{ProofTerm::hypothesis({index})}});
    return ProofTerm::linear_arithmetic(std::move(stated), cppl::kernel::ArithmeticCertificate{refutation});
}

// `premise -> goal`, closed by eliminating the evidence `inside` gives for
// `False` under that premise.
bool closes_under(const Proposition& premise, const Proposition& goal, const ProofTerm& inside) {
    const cppl::kernel::Context ctx;
    return cppl::kernel::check(ctx, Proposition::implication(premise, goal),
                               ProofTerm::implication_introduction(premise, ProofTerm::falsity_elimination(inside)), {})
        .has_value();
}

} // namespace

CPPL_TEST(false_elimination_closes_an_equality_of_structured_values) {
    // The goal no arithmetic states: two arbitrary pairs are equal. Under the
    // contradictory premise it closes, because the premise is refuted into
    // `False` and nothing about the goal is looked at.
    cppl::kernel::Context ctx;
    const auto pairs_equal = Proposition::equality(kPair, Term::variable(VarIndex{1}), Term::variable(VarIndex{0}));
    const auto goal =
        Proposition::for_all(kPair, Proposition::for_all(kPair, Proposition::implication(eq(0, 1), pairs_equal)));
    const auto evidence = ProofTerm::forall_introduction(
        kPair, ProofTerm::forall_introduction(
                   kPair, ProofTerm::implication_introduction(
                              eq(0, 1), ProofTerm::falsity_elimination(refutation_of_zero_is_one(ctx, 0)))));
    CPPL_CHECK(cppl::kernel::check(ctx, goal, evidence, {}).has_value());

    // The same evidence without the premise has nothing to refute: its fact
    // names a hypothesis no introduction placed.
    const auto unsupposed = Proposition::for_all(kPair, Proposition::for_all(kPair, pairs_equal));
    const auto stripped = ProofTerm::forall_introduction(
        kPair,
        ProofTerm::forall_introduction(kPair, ProofTerm::falsity_elimination(refutation_of_zero_is_one(ctx, 0))));
    const auto refused = cppl::kernel::check(ctx, unsupposed, stripped, {});
    CPPL_CHECK(!refused.has_value());
    CPPL_CHECK(refused.error().kind == RejectionKind::MalformedProofTerm);
}

CPPL_TEST(false_elimination_closes_a_goal_of_every_shape) {
    // Every proposition form, each false or unprovable on its own, so only the
    // eliminated contradiction can be closing it.
    const cppl::kernel::Context ctx;
    const auto absurd = refutation_of_zero_is_one(ctx, 0);
    const Proposition goals[] = {
        eq(5, 9),
        Proposition::equality(kPair, Term::variable(VarIndex{0}), Term::variable(VarIndex{0})),
        Proposition::for_all(u32(), Proposition::equality(u32(), var0(), literal(3))),
        Proposition::implication(eq(1, 1), eq(1, 2)),
        Proposition::conjunction(eq(1, 2), eq(3, 4)),
        Proposition::disjunction(eq(1, 2), eq(3, 4)),
        Proposition::falsity(),
    };
    for (std::size_t index = 0; index < std::size(goals); ++index) {
        // The structured goal mentions a variable, so it is stated under a pair.
        if (index == 1) {
            const auto goal = Proposition::for_all(kPair, Proposition::implication(eq(0, 1), goals[index]));
            const auto evidence = ProofTerm::forall_introduction(
                kPair, ProofTerm::implication_introduction(eq(0, 1), ProofTerm::falsity_elimination(absurd)));
            CPPL_CHECK(cppl::kernel::check(ctx, goal, evidence, {}).has_value());
            continue;
        }
        CPPL_CHECK(closes_under(eq(0, 1), goals[index], absurd));
    }
}

CPPL_TEST(false_elimination_needs_evidence_for_false_itself) {
    const cppl::kernel::Context ctx;
    const auto goal = Proposition::equality(u32(), literal(5), literal(9));

    // Reflexivity establishes an equality, never `False`.
    CPPL_CHECK(!closes_under(eq(0, 1), goal, ProofTerm::reflexivity()));

    // A hypothesis that is an absurd equality is still an equality: it is not
    // evidence for `False` until arithmetic refutes it.
    CPPL_CHECK(!closes_under(eq(0, 1), goal, ProofTerm::hypothesis({0})));

    // A satisfiable fact refutes nothing, whatever the certificate claims.
    for (std::uint32_t constraint = 0; constraint < 4; ++constraint) {
        cppl::kernel::FarkasSum forged;
        forged.multipliers.emplace_back(constraint, cppl::kernel::Wide{1});
        std::vector<cppl::kernel::ArithmeticFact> stated;
        stated.push_back(
            cppl::kernel::ArithmeticFact{eq(0, 0), cppl::kernel::Box<ProofTerm>{ProofTerm::hypothesis({0})}});
        CPPL_CHECK(!closes_under(
            eq(0, 0), goal,
            ProofTerm::linear_arithmetic(std::move(stated), cppl::kernel::ArithmeticCertificate{forged})));
    }

    // No facts at all: an empty system has a solution.
    CPPL_CHECK(!closes_under(
        eq(0, 1), goal,
        ProofTerm::linear_arithmetic({}, cppl::kernel::ArithmeticCertificate{cppl::kernel::FarkasSum{}})));

    // The right fact with a certificate naming a constraint that is not there.
    auto malformed = refutation_of_zero_is_one(ctx, 0);
    std::get<cppl::kernel::FarkasSum>(std::get<cppl::kernel::LinearArithmetic>(malformed.node).certificate.node)
        .multipliers.front()
        .first = 99;
    CPPL_CHECK(!closes_under(eq(0, 1), goal, malformed));

    // A fact restated as something its evidence does not establish.
    auto restated = refutation_of_zero_is_one(ctx, 0);
    std::get<cppl::kernel::LinearArithmetic>(restated.node).facts.front().proposition = eq(0, 2);
    CPPL_CHECK(!closes_under(eq(0, 1), goal, restated));
}

CPPL_TEST(false_has_no_introduction) {
    // `False` on its own, with nothing in scope, is never established.
    const cppl::kernel::Context ctx;
    const std::array attempts{
        ProofTerm::reflexivity(),
        ProofTerm::conjunction_introduction(ProofTerm::reflexivity(), ProofTerm::reflexivity()),
        ProofTerm::disjunction_introduction(ProofTerm::reflexivity(), false),
        ProofTerm::forall_introduction(u32(), ProofTerm::reflexivity()),
        ProofTerm::implication_introduction(eq(0, 1), ProofTerm::reflexivity()),
        ProofTerm::falsity_elimination(ProofTerm::reflexivity()),
        ProofTerm::linear_arithmetic({}, cppl::kernel::ArithmeticCertificate{cppl::kernel::FarkasSum{}}),
    };
    for (const ProofTerm& attempt : attempts) {
        const auto result = cppl::kernel::check(ctx, Proposition::falsity(), attempt, {});
        CPPL_CHECK(!result.has_value());
    }
    const auto refused = cppl::kernel::check(ctx, Proposition::falsity(), ProofTerm::reflexivity(), {});
    CPPL_CHECK(!refused.has_value());
    CPPL_CHECK(refused.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(false_is_refuted_from_the_facts_alone) {
    // The negation of `False` states no constraint, so a certificate that leans
    // on a negated goal has nothing to lean on. `x = 0` proves `x = 0`: the
    // system holds the fact and the goal's negation, and together they are
    // refuted. The same certificate for `False` is refused, because the fact
    // alone is satisfiable.
    cppl::kernel::Context ctx;
    const auto fact = Proposition::equality(u32(), var0(), zero32());
    const Proposition facts[] = {fact};
    const auto with_goal = cppl::kernel::arithmetic_system(ctx, facts, fact, CoreLimits{});
    const auto alone = cppl::kernel::arithmetic_system(ctx, facts, Proposition::falsity(), CoreLimits{});
    CPPL_CHECK(with_goal.has_value());
    CPPL_CHECK(alone.has_value());
    // The negated goal `x != 0` is the one disjunction; `False` adds none.
    CPPL_CHECK_EQ(with_goal->disjunctions.size(), std::size_t{1});
    CPPL_CHECK(alone->disjunctions.empty());
    CPPL_CHECK_EQ(alone->constraints.size(), with_goal->constraints.size());

    // Every single-constraint certificate over what `False` states is refused.
    for (std::uint32_t constraint = 0; constraint < 8; ++constraint) {
        cppl::kernel::FarkasSum forged;
        forged.multipliers.emplace_back(constraint, cppl::kernel::Wide{1});
        std::vector<cppl::kernel::ArithmeticFact> stated;
        stated.push_back(cppl::kernel::ArithmeticFact{fact, cppl::kernel::Box<ProofTerm>{ProofTerm::hypothesis({0})}});
        const auto goal = Proposition::for_all(u32(), Proposition::implication(fact, Proposition::falsity()));
        const auto evidence = ProofTerm::forall_introduction(
            u32(),
            ProofTerm::implication_introduction(
                fact, ProofTerm::linear_arithmetic(std::move(stated), cppl::kernel::ArithmeticCertificate{forged})));
        CPPL_CHECK(!cppl::kernel::check(ctx, goal, evidence, {}).has_value());
    }
}

CPPL_TEST(a_supposed_false_premise_is_restated_under_binders) {
    // A premise of `False` placed outside a quantifier is still `False` beneath
    // it: it mentions no variable, so restating it changes nothing.
    const cppl::kernel::Context ctx;
    const auto inner = Proposition::for_all(u32(), Proposition::equality(u32(), var0(), literal(5)));
    const auto goal = Proposition::implication(Proposition::falsity(), inner);
    const auto evidence = ProofTerm::implication_introduction(
        Proposition::falsity(),
        ProofTerm::forall_introduction(u32(), ProofTerm::falsity_elimination(ProofTerm::hypothesis({0}))));
    CPPL_CHECK(cppl::kernel::check(ctx, goal, evidence, {}).has_value());

    // The hypothesis names `False` only where it was supposed.
    const auto unsupposed =
        ProofTerm::forall_introduction(u32(), ProofTerm::falsity_elimination(ProofTerm::hypothesis({0})));
    const auto refused = cppl::kernel::check(ctx, inner, unsupposed, {});
    CPPL_CHECK(!refused.has_value());
    CPPL_CHECK(refused.error().kind == RejectionKind::MalformedProofTerm);

    // And a premise that is some other proposition is not `False`.
    const auto wrong = ProofTerm::implication_introduction(
        eq(0, 0), ProofTerm::forall_introduction(u32(), ProofTerm::falsity_elimination(ProofTerm::hypothesis({0}))));
    CPPL_CHECK(!cppl::kernel::check(ctx, Proposition::implication(eq(0, 0), inner), wrong, {}).has_value());
}

CPPL_TEST(falsity_is_inert_under_substitution) {
    const auto falsity = Proposition::falsity();
    CPPL_CHECK(cppl::kernel::shift(falsity, 3) == falsity);
    CPPL_CHECK(cppl::kernel::instantiate(falsity, literal(7)) == falsity);
    CPPL_CHECK(cppl::kernel::describe(falsity) == "False");
    CPPL_CHECK(falsity != eq(0, 1));
}
