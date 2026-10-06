// The kernel's checking tests (kernel_check_test): implication, hypotheses
// and the premises that discharge them.

#include "check_test_support.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

using namespace check_test_detail;

namespace {

// 41 + 1 = 41, which no amount of normalization makes true.
Proposition unreachable_equality() {
    return Proposition::equality(unsigned32(), sum(Term::literal(kUnsigned32, 41), Term::literal(kUnsigned32, 1)),
                                 Term::literal(kUnsigned32, 41));
}

Proposition settled_equality() {
    return Proposition::equality(unsigned32(), Term::literal(kUnsigned32, 41), Term::literal(kUnsigned32, 41));
}

} // namespace

CPPL_TEST(an_implication_is_introduced_by_supposing_its_premise) {
    const cppl::kernel::Context context;
    const Proposition goal = Proposition::implication(unreachable_equality(), settled_equality());

    const auto result = cppl::kernel::check(
        context, goal, ProofTerm::implication_introduction(unreachable_equality(), ProofTerm::reflexivity()),
        CoreLimits{});

    CPPL_CHECK(result.has_value());
    CPPL_CHECK(result->proposition() == goal);

    // The premise is supposed, never established: evidence that skips the
    // introduction is not evidence for the implication.
    const auto bare = cppl::kernel::check(context, goal, ProofTerm::reflexivity(), CoreLimits{});
    CPPL_CHECK(!bare.has_value());
    CPPL_CHECK(bare.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(evidence_must_suppose_the_premise_the_goal_states) {
    const cppl::kernel::Context context;
    const Proposition goal = Proposition::implication(unreachable_equality(), settled_equality());

    const auto result = cppl::kernel::check(
        context, goal, ProofTerm::implication_introduction(settled_equality(), ProofTerm::reflexivity()), CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(a_hypothesis_proves_the_premise_that_introduced_it) {
    const cppl::kernel::Context context;

    // P -> P, where P is false. Nothing but the hypothesis can close it, so
    // this goal is out of reach of every rule the kernel had before.
    const Proposition goal = Proposition::implication(unreachable_equality(), unreachable_equality());

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::implication_introduction(unreachable_equality(), ProofTerm::hypothesis(HypothesisIndex{0})),
        CoreLimits{});

    CPPL_CHECK(result.has_value());
    CPPL_CHECK(result->proposition() == goal);

    const auto by_reflexivity = cppl::kernel::check(
        context, goal, ProofTerm::implication_introduction(unreachable_equality(), ProofTerm::reflexivity()),
        CoreLimits{});
    CPPL_CHECK(!by_reflexivity.has_value());
    CPPL_CHECK(by_reflexivity.error().kind == RejectionKind::NotDefinitionallyEqual);
}

CPPL_TEST(a_hypothesis_no_introduction_placed_in_scope_is_rejected) {
    const cppl::kernel::Context context;

    const auto result =
        cppl::kernel::check(context, unreachable_equality(), ProofTerm::hypothesis(HypothesisIndex{0}), CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::MalformedProofTerm);
}

CPPL_TEST(a_hypothesis_standing_for_a_different_premise_is_rejected) {
    const cppl::kernel::Context context;

    // Supposing 41 = 41 says nothing about 41 + 1 = 41.
    const Proposition goal = Proposition::implication(settled_equality(), unreachable_equality());

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::implication_introduction(settled_equality(), ProofTerm::hypothesis(HypothesisIndex{0})),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(a_premise_leaves_scope_with_the_implication_that_introduced_it) {
    const cppl::kernel::Context context;

    // (P -> P) -> P, offered evidence that names the innermost hypothesis for
    // both roles. Only (P -> P) is in scope, so it does not stand for P.
    const Proposition conditional = Proposition::implication(unreachable_equality(), unreachable_equality());
    const Proposition goal = Proposition::implication(conditional, unreachable_equality());

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::implication_introduction(
            conditional, ProofTerm::implication_elimination(conditional, ProofTerm::hypothesis(HypothesisIndex{0}),
                                                            ProofTerm::hypothesis(HypothesisIndex{0}))),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(discharging_a_premise_establishes_the_conclusion) {
    const cppl::kernel::Context context = with_identity();

    const Proposition premise = Proposition::equality(
        unsigned32(), Term::call(DefId{0}, {Term::literal(kUnsigned32, 41)}), Term::literal(kUnsigned32, 41));
    const Proposition goal = settled_equality();
    const Proposition conditional = Proposition::implication(premise, goal);

    const auto result =
        cppl::kernel::check(context, goal,
                            ProofTerm::implication_elimination(
                                conditional, ProofTerm::implication_introduction(premise, ProofTerm::reflexivity()),
                                ProofTerm::reflexivity()),
                            CoreLimits{});

    CPPL_CHECK(result.has_value());
    CPPL_CHECK(result->proposition() == goal);
}

CPPL_TEST(a_premise_offered_without_evidence_is_rejected) {
    const cppl::kernel::Context context;

    const Proposition conditional = Proposition::implication(unreachable_equality(), settled_equality());

    // The implication holds, and the premise handed to it does not.
    const auto result = cppl::kernel::check(
        context, settled_equality(),
        ProofTerm::implication_elimination(
            conditional, ProofTerm::implication_introduction(unreachable_equality(), ProofTerm::reflexivity()),
            ProofTerm::reflexivity()),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::NotDefinitionallyEqual);
}

CPPL_TEST(a_premise_discharged_against_something_other_than_an_implication_is_rejected) {
    const cppl::kernel::Context context;

    const auto result = cppl::kernel::check(
        context, settled_equality(),
        ProofTerm::implication_elimination(settled_equality(), ProofTerm::reflexivity(), ProofTerm::reflexivity()),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(a_hypothesis_is_restated_under_the_binders_it_is_used_beneath) {
    const cppl::kernel::Context context;

    // forall x. (x + 1 = x) -> forall y. (x + 1 = x)
    //
    // Under the second binder the premise's variable is one level further out,
    // so the hypothesis has to be restated before it can close the goal.
    const Proposition premise_at_x =
        Proposition::equality(unsigned32(), sum(bound(), Term::literal(kUnsigned32, 1)), bound());
    const Proposition premise_at_x_under_y =
        Proposition::equality(unsigned32(), sum(outer(), Term::literal(kUnsigned32, 1)), outer());

    const ProofTerm evidence = ProofTerm::forall_introduction(
        unsigned32(),
        ProofTerm::implication_introduction(
            premise_at_x, ProofTerm::forall_introduction(unsigned32(), ProofTerm::hypothesis(HypothesisIndex{0}))));

    const Proposition goal = Proposition::for_all(
        unsigned32(), Proposition::implication(premise_at_x, Proposition::for_all(unsigned32(), premise_at_x_under_y)));

    const auto result = cppl::kernel::check(context, goal, evidence, CoreLimits{});
    CPPL_CHECK(result.has_value());

    // Had the hypothesis not been restated, it would have named the inner
    // binder and this different statement would have been accepted.
    const Proposition captured = Proposition::for_all(
        unsigned32(), Proposition::implication(premise_at_x, Proposition::for_all(unsigned32(), premise_at_x)));

    CPPL_CHECK(!cppl::kernel::check(context, captured, evidence, CoreLimits{}).has_value());
}
