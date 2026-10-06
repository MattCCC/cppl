// Arithmetic evidence tests (unit_arithmetic_test): automation's determinism
// and the kernel's authority over it, forged proof terms, core limits,
// exhaustive small widths, and the decidable boundary.

#include "arithmetic_test_support.hpp"
#include "cppl/automation/evidence.hpp"
#include "cppl/kernel/box.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/testing/test.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace arithmetic_test_detail;

// -----------------------------------------------------------------------------
// Automation determinism and kernel authority.
// -----------------------------------------------------------------------------

CPPL_TEST(proposal_for_simple_arithmetic_goal_is_deterministic) {
    const auto x = var(0);
    const auto goal = closed(kU32, 1, {holds(k::PrimOp::Less, kU32, x, lit(kU32, 10))},
                             holds(k::PrimOp::LessEqual, kU32, x, lit(kU32, 10)));
    const k::Context context;
    const auto first = proposal(context, goal);
    const auto second = proposal(context, goal);
    CPPL_CHECK(first.has_value());
    CPPL_CHECK(second.has_value());
    CPPL_CHECK_EQ(first->strategy, second->strategy);
    CPPL_CHECK_EQ(first->proof, second->proof);
}

CPPL_TEST(successful_proposal_names_a_nonempty_strategy) {
    const auto x = var(0);
    const auto goal = closed(kU32, 1, {}, equal(kU32, x, x));
    const k::Context context;
    const auto evidence = proposal(context, goal);
    CPPL_CHECK(evidence.has_value());
    CPPL_CHECK(!evidence->strategy.empty());
}

CPPL_TEST(every_successful_proposal_in_this_path_is_kernel_accepted) {
    const auto x = var(0);
    const auto goal = closed(kU32, 1, {holds(k::PrimOp::Less, kU32, x, lit(kU32, 10))},
                             holds(k::PrimOp::LessEqual, kU32, add(kU32, x, lit(kU32, 1)), lit(kU32, 10)));
    const k::Context context;
    const auto evidence = proposal(context, goal);
    CPPL_CHECK(evidence.has_value());
    CPPL_CHECK(accepted(context, goal, evidence->proof));
}

CPPL_TEST(kernel_acceptance_carries_exact_goal) {
    const auto x = var(0);
    const auto goal = closed(kU32, 1, {}, equal(kU32, x, x));
    const k::Context context;
    const auto evidence = proposal(context, goal);
    CPPL_CHECK(evidence.has_value());
    const auto result = k::check(context, goal, evidence->proof, {});
    CPPL_CHECK(result.has_value());
    CPPL_CHECK_EQ(result->proposition(), goal);
}

CPPL_TEST(evidence_for_one_goal_is_rejected_for_stronger_goal) {
    const auto n = var(1);
    const auto i = var(0);
    const auto preserved =
        closed(kU32, 2, {holds(k::PrimOp::LessEqual, kU32, i, n), holds(k::PrimOp::Less, kU32, i, n)},
               holds(k::PrimOp::LessEqual, kU32, add(kU32, i, lit(kU32, 1)), n));
    const auto stronger = closed(kU32, 2, {holds(k::PrimOp::LessEqual, kU32, i, n), holds(k::PrimOp::Less, kU32, i, n)},
                                 holds(k::PrimOp::Less, kU32, add(kU32, i, lit(kU32, 1)), n));

    const k::Context context;
    const auto evidence = proposal(context, preserved);
    CPPL_CHECK(evidence.has_value());
    CPPL_CHECK(accepted(context, preserved, evidence->proof));
    CPPL_CHECK(!accepted(context, stronger, evidence->proof));
}

CPPL_TEST(evidence_for_reflexive_goal_is_rejected_for_false_same_shape_goal) {
    const auto x = var(0);
    const auto first = closed(kU32, 1, {}, equal(kU32, x, x));
    const auto second = closed(kU32, 1, {}, equal(kU32, add(kU32, x, lit(kU32, 1)), x));
    const k::Context context;
    const auto evidence = proposal(context, first);
    CPPL_CHECK(evidence.has_value());
    CPPL_CHECK(accepted(context, first, evidence->proof));
    CPPL_CHECK(!accepted(context, second, evidence->proof));
}

// -----------------------------------------------------------------------------
// Direct adversarial proof terms.
// -----------------------------------------------------------------------------

CPPL_TEST(reflexivity_cannot_prove_distinct_literals) {
    const k::Context context;
    const auto goal = equal(kU8, lit(kU8, 1), lit(kU8, 2));
    const auto result = k::check(context, goal, k::ProofTerm::reflexivity(), {});
    CPPL_CHECK(!result.has_value());
    CPPL_CHECK_EQ(result.error().kind, k::RejectionKind::NotDefinitionallyEqual);
}

CPPL_TEST(hypothesis_cannot_be_used_without_an_introduced_premise) {
    const k::Context context;
    const auto goal = equal(kU8, lit(kU8, 1), lit(kU8, 1));
    const auto result = k::check(context, goal, k::ProofTerm::hypothesis(k::HypothesisIndex{0}), {});
    CPPL_CHECK(!result.has_value());
}

CPPL_TEST(out_of_range_hypothesis_index_is_rejected) {
    const auto premise = equal(kU8, lit(kU8, 1), lit(kU8, 1));
    const auto goal = k::Proposition::implication(premise, equal(kU8, lit(kU8, 1), lit(kU8, 1)));
    const auto proof = k::ProofTerm::implication_introduction(premise, k::ProofTerm::hypothesis(k::HypothesisIndex{1}));
    const k::Context context;
    CPPL_CHECK(!k::check(context, goal, proof, {}).has_value());
}

CPPL_TEST(forall_introduction_with_wrong_binder_is_rejected) {
    const auto goal = k::Proposition::for_all(type(kU32), equal(kU32, var(0), var(0)));
    const auto proof = k::ProofTerm::forall_introduction(type(kU64), k::ProofTerm::reflexivity());
    const k::Context context;
    CPPL_CHECK(!k::check(context, goal, proof, {}).has_value());
}

CPPL_TEST(implication_introduction_with_wrong_premise_is_rejected) {
    const auto real_premise = equal(kU8, lit(kU8, 1), lit(kU8, 1));
    const auto fake_premise = equal(kU8, lit(kU8, 2), lit(kU8, 2));
    const auto goal = k::Proposition::implication(real_premise, real_premise);
    const auto proof =
        k::ProofTerm::implication_introduction(fake_premise, k::ProofTerm::hypothesis(k::HypothesisIndex{0}));
    const k::Context context;
    CPPL_CHECK(!k::check(context, goal, proof, {}).has_value());
}

CPPL_TEST(empty_arithmetic_certificate_cannot_magic_a_false_goal) {
    const auto proof = k::ProofTerm::linear_arithmetic({}, empty_farkas());
    const auto goal = equal(kU8, lit(kU8, 1), lit(kU8, 2));
    const k::Context context;
    CPPL_CHECK(!k::check(context, goal, proof, {}).has_value());
}

CPPL_TEST(arithmetic_certificate_with_out_of_range_constraint_reference_is_rejected) {
    const auto proof = k::ProofTerm::linear_arithmetic({}, impossible_multiplier_certificate());
    const auto goal = holds(k::PrimOp::Less, kU8, lit(kU8, 1), lit(kU8, 2));
    const k::Context context;
    CPPL_CHECK(!k::check(context, goal, proof, {}).has_value());
}

CPPL_TEST(arithmetic_fact_with_invalid_evidence_is_not_trusted) {
    const auto fact_proposition = equal(kU8, lit(kU8, 1), lit(kU8, 2));
    k::ArithmeticFact fake_fact{fact_proposition, k::Box<k::ProofTerm>{k::ProofTerm::reflexivity()}};
    const auto proof = k::ProofTerm::linear_arithmetic({std::move(fake_fact)}, empty_farkas());
    const auto goal = equal(kU8, lit(kU8, 7), lit(kU8, 9));
    const k::Context context;
    CPPL_CHECK(!k::check(context, goal, proof, {}).has_value());
}

// -----------------------------------------------------------------------------
// Core limits must fail closed.
// -----------------------------------------------------------------------------

CPPL_TEST(zero_term_depth_limit_rejects_nontrivial_goal) {
    const auto goal = equal(kU8, add(kU8, lit(kU8, 1), lit(kU8, 2)), lit(kU8, 3));
    const k::Context context;
    const auto evidence = proposal(context, goal);
    CPPL_CHECK(evidence.has_value());

    k::CoreLimits limits;
    limits.max_term_depth = 0;
    CPPL_CHECK(!k::check(context, goal, evidence->proof, limits).has_value());
}

CPPL_TEST(zero_normalization_budget_rejects_definition_unfolding) {
    k::Context context;
    const k::DefId id{1};
    CPPL_CHECK(context.define(unary_definition(id, "inc", kU8, kU8, add(kU8, var(0), lit(kU8, 1)))).has_value());

    const auto x = var(0);
    const auto goal = closed(kU8, 1, {}, equal(kU8, k::Term::call(id, {x}), add(kU8, x, lit(kU8, 1))));
    const auto evidence = proposal(context, goal);
    CPPL_CHECK(evidence.has_value());

    k::CoreLimits limits;
    limits.max_normalization_steps = 0;
    CPPL_CHECK(!k::check(context, goal, evidence->proof, limits).has_value());
}

CPPL_TEST(zero_arithmetic_fact_limit_rejects_fact_using_arithmetic_proof) {
    const auto x = var(0);
    const auto goal = closed(kU32, 1, {holds(k::PrimOp::Less, kU32, x, lit(kU32, 10))},
                             holds(k::PrimOp::LessEqual, kU32, x, lit(kU32, 10)));
    const k::Context context;
    const auto evidence = proposal(context, goal);
    CPPL_CHECK(evidence.has_value());

    k::CoreLimits limits;
    limits.max_arithmetic_facts = 0;
    CPPL_CHECK(!k::check(context, goal, evidence->proof, limits).has_value());
}

CPPL_TEST(zero_certificate_node_limit_rejects_arithmetic_certificate) {
    const auto x = var(0);
    const auto goal = closed(kU32, 1, {holds(k::PrimOp::Less, kU32, x, lit(kU32, 10))},
                             holds(k::PrimOp::LessEqual, kU32, add(kU32, x, lit(kU32, 1)), lit(kU32, 10)));
    const k::Context context;
    const auto evidence = proposal(context, goal);
    CPPL_CHECK(evidence.has_value());

    k::CoreLimits limits;
    limits.max_certificate_nodes = 0;
    CPPL_CHECK(!k::check(context, goal, evidence->proof, limits).has_value());
}

// -----------------------------------------------------------------------------
// Small-width exhaustive pressure cases. These are theorem-level tests over the
// whole finite domain, not host-language spot checks.
// -----------------------------------------------------------------------------

CPPL_TEST(unsigned_1_bit_increment_toggles) {
    const auto x = var(0);
    const auto goal = closed(kU1, 1, {}, equal(kU1, add(kU1, x, lit(kU1, 1)), sub(kU1, lit(kU1, 1), x)));
    CPPL_CHECK(proven(goal));
}

CPPL_TEST(unsigned_2_bit_add_four_is_identity_modulo_four) {
    const auto x = var(0);
    // 4 is not representable as a u2 literal, so express it as 3 + 1.
    const auto four_mod = add(kU2, lit(kU2, 3), lit(kU2, 1));
    CPPL_CHECK(proven(closed(kU2, 1, {}, equal(kU2, add(kU2, x, four_mod), x))));
}

CPPL_TEST(signed_1_bit_domain_is_minus_one_or_zero) {
    const auto x = var(0);
    const auto goal = k::Proposition::disjunction(equal(kI1, x, lit(kI1, -1)), equal(kI1, x, lit(kI1, 0)));
    CPPL_CHECK(proven(closed(kI1, 1, {}, goal)));
}

CPPL_TEST(unsigned_2_bit_domain_is_one_of_four_values) {
    const auto x = var(0);
    const auto goal = k::Proposition::disjunction(
        equal(kU2, x, lit(kU2, 0)),
        k::Proposition::disjunction(
            equal(kU2, x, lit(kU2, 1)),
            k::Proposition::disjunction(equal(kU2, x, lit(kU2, 2)), equal(kU2, x, lit(kU2, 3)))));
    CPPL_CHECK(proven(closed(kU2, 1, {}, goal)));
}

// -----------------------------------------------------------------------------
// Adversarial cases for the case-analysis path. A decidable split is allowed to
// close an exhaustive disjunction, and must close nothing else: every goal here
// is false, or does not follow, and the kernel must refuse each one.
// -----------------------------------------------------------------------------

CPPL_TEST(incomplete_enumeration_of_a_wide_domain_is_not_proven) {
    // u32 has far more than four values, so this enumeration is false.
    const auto x = var(0);
    const auto goal = k::Proposition::disjunction(
        equal(kU32, x, lit(kU32, 0)),
        k::Proposition::disjunction(
            equal(kU32, x, lit(kU32, 1)),
            k::Proposition::disjunction(equal(kU32, x, lit(kU32, 2)), equal(kU32, x, lit(kU32, 3)))));
    CPPL_CHECK(not_accepted_from_automation(closed(kU32, 1, {}, goal)));
}

CPPL_TEST(two_sided_disjunction_over_a_wide_domain_is_not_proven) {
    const auto x = var(0);
    const auto goal = k::Proposition::disjunction(equal(kU32, x, lit(kU32, 0)), equal(kU32, x, lit(kU32, 1)));
    CPPL_CHECK(not_accepted_from_automation(closed(kU32, 1, {}, goal)));
}

CPPL_TEST(trichotomy_without_its_equality_case_is_not_proven) {
    const auto y = var(0);
    const auto x = var(1);
    const auto goal =
        k::Proposition::disjunction(holds(k::PrimOp::Less, kI8, x, y), holds(k::PrimOp::Greater, kI8, x, y));
    CPPL_CHECK(not_accepted_from_automation(closed(kI8, 2, {}, goal)));
}

CPPL_TEST(repeating_one_side_does_not_make_a_disjunction_exhaustive) {
    const auto x = var(0);
    const auto goal = k::Proposition::disjunction(equal(kU32, x, lit(kU32, 0)), equal(kU32, x, lit(kU32, 0)));
    CPPL_CHECK(not_accepted_from_automation(closed(kU32, 1, {}, goal)));
}

CPPL_TEST(case_analysis_does_not_recover_a_side_from_a_disjunctive_premise) {
    const auto x = var(0);
    const auto premise = k::Proposition::disjunction(equal(kU32, x, lit(kU32, 0)), equal(kU32, x, lit(kU32, 1)));
    CPPL_CHECK(not_accepted_from_automation(closed(kU32, 1, {premise}, equal(kU32, x, lit(kU32, 0)))));
    CPPL_CHECK(not_accepted_from_automation(closed(kU32, 1, {premise}, equal(kU32, x, lit(kU32, 1)))));
}

CPPL_TEST(case_analysis_still_requires_every_case_to_support_the_goal) {
    const auto x = var(0);
    const auto premise = k::Proposition::disjunction(equal(kU32, x, lit(kU32, 0)), equal(kU32, x, lit(kU32, 5)));
    CPPL_CHECK(
        not_accepted_from_automation(closed(kU32, 1, {premise}, holds(k::PrimOp::LessEqual, kU32, x, lit(kU32, 1)))));
}

CPPL_TEST(a_decidable_split_does_not_prove_a_false_equality) {
    CPPL_CHECK(not_accepted_from_automation(equal(kU32, lit(kU32, 0), lit(kU32, 1))));
}

// -----------------------------------------------------------------------------
// The boundary between what is decidable and what this automation constructs.
//
// These goals are true, and decidable: every machine type is finite and its
// equality is decided by the machine. Automation declines them because the
// enumeration principle it uses costs one case per value, and it will not spend
// 2^32 of them. That is a resource policy of the search, not a statement that
// the proposition lacks a truth value or that the core denies it.
//
// The distinction matters for the trust model: "automation did not synthesize a
// proof" and "the proposition is false" are different outcomes, and only the
// first one is what these record.
// -----------------------------------------------------------------------------

CPPL_TEST(a_complementary_pair_is_decided_by_order_at_any_width) {
    // forall x:u32. x == 0 || x != 0
    //
    // Equality and its negation are order relations on one pair, so the order
    // principle settles this and no width threshold applies. The width of the
    // type is irrelevant to whether the proposition is decidable.
    const auto x = var(0);
    const auto goal =
        k::Proposition::disjunction(equal(kU32, x, lit(kU32, 0)), holds(k::PrimOp::NotEqual, kU32, x, lit(kU32, 0)));
    CPPL_CHECK(proven(closed(kU32, 1, {}, goal)));
}

CPPL_TEST(automation_declines_to_enumerate_a_wide_domain_it_could_in_principle_decide) {
    // forall x:u8. x == 0 || x == 1 || ... || x == 255
    //
    // True, and decidable: u8 is finite and its equality is decided by the
    // machine. No two sides are the same pair, so the order principle does not
    // apply and only enumeration would settle it -- at one case per value.
    // Automation declines to spend 256 of them. That is this search's cost
    // policy, not a claim that the proposition lacks a truth value.
    const auto x = var(0);
    k::Proposition goal = equal(kU8, x, lit(kU8, 255));
    for (std::int64_t value = 254; value >= 0; --value) {
        goal = k::Proposition::disjunction(equal(kU8, x, lit(kU8, value)), std::move(goal));
    }
    CPPL_CHECK(not_accepted_from_automation(closed(kU8, 1, {}, goal)));
}

CPPL_TEST(the_same_goal_at_an_enumerable_width_is_proven) {
    // The u2 instance of the goal above. Nothing about the proposition changed
    // but the width, which is what makes the previous case a cost decision.
    const auto x = var(0);
    const auto goal =
        k::Proposition::disjunction(equal(kU2, x, lit(kU2, 0)), holds(k::PrimOp::NotEqual, kU2, x, lit(kU2, 0)));
    CPPL_CHECK(proven(closed(kU2, 1, {}, goal)));
}

CPPL_TEST(a_declined_width_is_still_decided_where_order_settles_it) {
    // Order is total and decided at every width, so the order principle carries
    // no threshold: this u32 trichotomy is proven though the enumeration
    // principle would have declined the same type.
    const auto y = var(0);
    const auto x = var(1);
    const auto goal = k::Proposition::disjunction(
        holds(k::PrimOp::Less, kU32, x, y),
        k::Proposition::disjunction(equal(kU32, x, y), holds(k::PrimOp::Greater, kU32, x, y)));
    CPPL_CHECK(proven(closed(kU32, 2, {}, goal)));
}

CPPL_TEST(a_goal_automation_declines_is_still_established_case_by_case) {
    // What the search will not assemble in one step is not thereby out of
    // reach: the cases of the declined u8 enumeration are each provable, so the
    // derivation exists even where automation does not spend the effort to
    // build it. The trust boundary is the kernel's check, not the search's
    // willingness to look.
    const auto x = var(0);
    const auto is_zero = holds(k::PrimOp::Equal, kU8, x, lit(kU8, 0));
    const auto is_one = holds(k::PrimOp::Equal, kU8, x, lit(kU8, 1));
    CPPL_CHECK(proven(closed(kU8, 1, {is_zero}, equal(kU8, x, lit(kU8, 0)))));
    CPPL_CHECK(proven(closed(kU8, 1, {is_one}, equal(kU8, x, lit(kU8, 1)))));
}
