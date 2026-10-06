// Arithmetic evidence tests (unit_arithmetic_test): closed facts, order,
// wrap-aware steps, linear transport, connectives, quantifiers, conditional
// arithmetic and definition unfolding. arithmetic_test.cpp states the
// trust invariant under test.

#include "arithmetic_test_support.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/testing/test.hpp"

#include <string>
#include <utility>
#include <vector>

using namespace arithmetic_test_detail;

// -----------------------------------------------------------------------------
// Closed arithmetic facts and machine-range reasoning.
// -----------------------------------------------------------------------------

CPPL_TEST(reflexive_integer_equality_is_proven) {
    const auto x = var(0);
    CPPL_CHECK(proven(closed(kU32, 1, {}, equal(kU32, x, x))));
}

CPPL_TEST(distinct_constants_are_not_proven_equal) {
    CPPL_CHECK(not_accepted_from_automation(equal(kU32, lit(kU32, 1), lit(kU32, 2))));
}

CPPL_TEST(unsigned_variable_is_always_at_least_zero) {
    const auto x = var(0);
    CPPL_CHECK(proven(closed(kU8, 1, {}, holds(k::PrimOp::GreaterEqual, kU8, x, lit(kU8, 0)))));
}

CPPL_TEST(unsigned_8_variable_is_always_at_most_255) {
    const auto x = var(0);
    CPPL_CHECK(proven(closed(kU8, 1, {}, holds(k::PrimOp::LessEqual, kU8, x, lit(kU8, 255)))));
}

CPPL_TEST(signed_8_variable_is_always_at_least_minus_128) {
    const auto x = var(0);
    CPPL_CHECK(proven(closed(kI8, 1, {}, holds(k::PrimOp::GreaterEqual, kI8, x, lit(kI8, -128)))));
}

CPPL_TEST(signed_8_variable_is_always_at_most_127) {
    const auto x = var(0);
    CPPL_CHECK(proven(closed(kI8, 1, {}, holds(k::PrimOp::LessEqual, kI8, x, lit(kI8, 127)))));
}

CPPL_TEST(unsigned_8_variable_is_not_always_below_255) {
    const auto x = var(0);
    CPPL_CHECK(not_accepted_from_automation(closed(kU8, 1, {}, holds(k::PrimOp::Less, kU8, x, lit(kU8, 255)))));
}

CPPL_TEST(signed_8_variable_is_not_always_nonnegative) {
    const auto x = var(0);
    CPPL_CHECK(not_accepted_from_automation(closed(kI8, 1, {}, holds(k::PrimOp::GreaterEqual, kI8, x, lit(kI8, 0)))));
}

// -----------------------------------------------------------------------------
// Order reasoning.
// -----------------------------------------------------------------------------

CPPL_TEST(strict_order_is_transitive) {
    const auto z = var(0);
    const auto y = var(1);
    const auto x = var(2);
    CPPL_CHECK(proven(closed(kU32, 3, {holds(k::PrimOp::Less, kU32, x, y), holds(k::PrimOp::Less, kU32, y, z)},
                             holds(k::PrimOp::Less, kU32, x, z))));
}

CPPL_TEST(non_strict_order_is_transitive) {
    const auto z = var(0);
    const auto y = var(1);
    const auto x = var(2);
    CPPL_CHECK(
        proven(closed(kI32, 3, {holds(k::PrimOp::LessEqual, kI32, x, y), holds(k::PrimOp::LessEqual, kI32, y, z)},
                      holds(k::PrimOp::LessEqual, kI32, x, z))));
}

CPPL_TEST(strict_order_implies_non_strict_order) {
    const auto y = var(0);
    const auto x = var(1);
    CPPL_CHECK(proven(closed(kU32, 2, {holds(k::PrimOp::Less, kU32, x, y)}, holds(k::PrimOp::LessEqual, kU32, x, y))));
}

CPPL_TEST(antisymmetry_yields_equality) {
    const auto y = var(0);
    const auto x = var(1);
    CPPL_CHECK(
        proven(closed(kI32, 2, {holds(k::PrimOp::LessEqual, kI32, x, y), holds(k::PrimOp::LessEqual, kI32, y, x)},
                      equal(kI32, x, y))));
}

CPPL_TEST(strict_order_is_irreflexive) {
    const auto x = var(0);
    CPPL_CHECK(proven(closed(kI32, 1, {}, holds(k::PrimOp::Less, kI32, x, x, false))));
}

CPPL_TEST(strict_order_is_asymmetric) {
    const auto y = var(0);
    const auto x = var(1);
    CPPL_CHECK(
        proven(closed(kI32, 2, {holds(k::PrimOp::Less, kI32, x, y)}, holds(k::PrimOp::Less, kI32, y, x, false))));
}

CPPL_TEST(non_strict_order_does_not_imply_strict_order) {
    const auto y = var(0);
    const auto x = var(1);
    CPPL_CHECK(not_accepted_from_automation(
        closed(kI32, 2, {holds(k::PrimOp::LessEqual, kI32, x, y)}, holds(k::PrimOp::Less, kI32, x, y))));
}

CPPL_TEST(equality_implies_both_order_directions) {
    const auto y = var(0);
    const auto x = var(1);
    const auto premise = equal(kI32, x, y);
    const auto conclusion = k::Proposition::conjunction(holds(k::PrimOp::LessEqual, kI32, x, y),
                                                        holds(k::PrimOp::GreaterEqual, kI32, x, y));
    CPPL_CHECK(proven(closed(kI32, 2, {premise}, conclusion)));
}

CPPL_TEST(contradictory_strict_cycle_proves_arbitrary_arithmetic_goal) {
    const auto z = var(0);
    const auto y = var(1);
    const auto x = var(2);
    CPPL_CHECK(proven(closed(kU32, 3,
                             {holds(k::PrimOp::Less, kU32, x, y), holds(k::PrimOp::Less, kU32, y, z),
                              holds(k::PrimOp::LessEqual, kU32, z, x)},
                             equal(kU32, x, lit(kU32, 7)))));
}

CPPL_TEST(noncontradictory_equal_bounds_do_not_prove_arbitrary_value) {
    const auto y = var(0);
    const auto x = var(1);
    CPPL_CHECK(not_accepted_from_automation(
        closed(kU32, 2, {holds(k::PrimOp::LessEqual, kU32, x, y), holds(k::PrimOp::LessEqual, kU32, y, x)},
               equal(kU32, x, lit(kU32, 7)))));
}

// -----------------------------------------------------------------------------
// Wrap-aware increment/decrement reasoning.
// -----------------------------------------------------------------------------

CPPL_TEST(unsigned_increment_is_monotone_below_maximum) {
    const auto x = var(0);
    CPPL_CHECK(proven(closed(kU8, 1, {holds(k::PrimOp::Less, kU8, x, lit(kU8, 255))},
                             holds(k::PrimOp::Greater, kU8, add(kU8, x, lit(kU8, 1)), x))));
}

CPPL_TEST(unsigned_increment_is_not_globally_monotone) {
    const auto x = var(0);
    CPPL_CHECK(
        not_accepted_from_automation(closed(kU8, 1, {}, holds(k::PrimOp::Greater, kU8, add(kU8, x, lit(kU8, 1)), x))));
}

CPPL_TEST(signed_increment_is_monotone_below_maximum) {
    const auto x = var(0);
    CPPL_CHECK(proven(closed(kI8, 1, {holds(k::PrimOp::Less, kI8, x, lit(kI8, 127))},
                             holds(k::PrimOp::Greater, kI8, add(kI8, x, lit(kI8, 1)), x))));
}

CPPL_TEST(signed_increment_is_not_globally_monotone) {
    const auto x = var(0);
    CPPL_CHECK(
        not_accepted_from_automation(closed(kI8, 1, {}, holds(k::PrimOp::Greater, kI8, add(kI8, x, lit(kI8, 1)), x))));
}

CPPL_TEST(unsigned_decrement_is_monotone_above_zero) {
    const auto x = var(0);
    CPPL_CHECK(proven(closed(kU8, 1, {holds(k::PrimOp::Greater, kU8, x, lit(kU8, 0))},
                             holds(k::PrimOp::Less, kU8, sub(kU8, x, lit(kU8, 1)), x))));
}

CPPL_TEST(unsigned_decrement_is_not_globally_monotone) {
    const auto x = var(0);
    CPPL_CHECK(
        not_accepted_from_automation(closed(kU8, 1, {}, holds(k::PrimOp::Less, kU8, sub(kU8, x, lit(kU8, 1)), x))));
}

CPPL_TEST(signed_decrement_is_monotone_above_minimum) {
    const auto x = var(0);
    CPPL_CHECK(proven(closed(kI8, 1, {holds(k::PrimOp::Greater, kI8, x, lit(kI8, -128))},
                             holds(k::PrimOp::Less, kI8, sub(kI8, x, lit(kI8, 1)), x))));
}

CPPL_TEST(signed_decrement_is_not_globally_monotone) {
    const auto x = var(0);
    CPPL_CHECK(
        not_accepted_from_automation(closed(kI8, 1, {}, holds(k::PrimOp::Less, kI8, sub(kI8, x, lit(kI8, 1)), x))));
}

CPPL_TEST(loop_step_preserves_upper_bound_when_strictly_below_it) {
    const auto n = var(1);
    const auto i = var(0);
    CPPL_CHECK(proven(closed(kU32, 2, {holds(k::PrimOp::LessEqual, kU32, i, n), holds(k::PrimOp::Less, kU32, i, n)},
                             holds(k::PrimOp::LessEqual, kU32, add(kU32, i, lit(kU32, 1)), n))));
}

CPPL_TEST(loop_step_without_strict_bound_is_not_valid) {
    const auto n = var(1);
    const auto i = var(0);
    CPPL_CHECK(not_accepted_from_automation(closed(kU32, 2, {holds(k::PrimOp::LessEqual, kU32, i, n)},
                                                   holds(k::PrimOp::LessEqual, kU32, add(kU32, i, lit(kU32, 1)), n))));
}

CPPL_TEST(loop_exit_pins_counter_to_bound) {
    const auto n = var(1);
    const auto i = var(0);
    CPPL_CHECK(
        proven(closed(kU32, 2, {holds(k::PrimOp::LessEqual, kU32, i, n), holds(k::PrimOp::Less, kU32, i, n, false)},
                      equal(kU32, i, n))));
}

CPPL_TEST(loop_exit_without_invariant_does_not_pin_counter) {
    const auto n = var(1);
    const auto i = var(0);
    CPPL_CHECK(
        not_accepted_from_automation(closed(kU32, 2, {holds(k::PrimOp::Less, kU32, i, n, false)}, equal(kU32, i, n))));
}

// -----------------------------------------------------------------------------
// Linear expression and equality transport.
// -----------------------------------------------------------------------------

CPPL_TEST(add_zero_is_identity) {
    const auto x = var(0);
    CPPL_CHECK(proven(closed(kU32, 1, {}, equal(kU32, add(kU32, x, lit(kU32, 0)), x))));
}

CPPL_TEST(subtract_zero_is_identity) {
    const auto x = var(0);
    CPPL_CHECK(proven(closed(kI32, 1, {}, equal(kI32, sub(kI32, x, lit(kI32, 0)), x))));
}

CPPL_TEST(subtract_self_is_zero_modulo_machine_width) {
    const auto x = var(0);
    CPPL_CHECK(proven(closed(kU32, 1, {}, equal(kU32, sub(kU32, x, x), lit(kU32, 0)))));
}

CPPL_TEST(addition_is_commutative_modulo_machine_width) {
    const auto y = var(0);
    const auto x = var(1);
    CPPL_CHECK(proven(closed(kU32, 2, {}, equal(kU32, add(kU32, x, y), add(kU32, y, x)))));
}

CPPL_TEST(addition_is_associative_modulo_machine_width) {
    const auto z = var(0);
    const auto y = var(1);
    const auto x = var(2);
    CPPL_CHECK(proven(closed(kU16, 3, {}, equal(kU16, add(kU16, add(kU16, x, y), z), add(kU16, x, add(kU16, y, z))))));
}

CPPL_TEST(equal_terms_remain_equal_after_same_increment) {
    const auto y = var(0);
    const auto x = var(1);
    CPPL_CHECK(proven(
        closed(kU32, 2, {equal(kU32, x, y)}, equal(kU32, add(kU32, x, lit(kU32, 1)), add(kU32, y, lit(kU32, 1))))));
}

CPPL_TEST(equal_terms_can_be_substituted_into_order_relation) {
    const auto z = var(0);
    const auto y = var(1);
    const auto x = var(2);
    CPPL_CHECK(proven(
        closed(kI32, 3, {equal(kI32, x, y), holds(k::PrimOp::Less, kI32, y, z)}, holds(k::PrimOp::Less, kI32, x, z))));
}

CPPL_TEST(accumulator_follows_counter_equality_to_exit) {
    const auto a = var(3);
    const auto n = var(2);
    const auto i = var(1);
    const auto r = var(0);
    CPPL_CHECK(proven(closed(kU32, 4,
                             {equal(kU32, r, mul(kU32, a, i)), holds(k::PrimOp::LessEqual, kU32, i, n),
                              holds(k::PrimOp::Less, kU32, i, n, false)},
                             equal(kU32, r, mul(kU32, a, n)))));
}

CPPL_TEST(accumulator_without_exit_equality_is_not_pinned_to_final_product) {
    const auto a = var(3);
    const auto n = var(2);
    const auto i = var(1);
    const auto r = var(0);
    CPPL_CHECK(not_accepted_from_automation(
        closed(kU32, 4, {equal(kU32, r, mul(kU32, a, i)), holds(k::PrimOp::LessEqual, kU32, i, n)},
               equal(kU32, r, mul(kU32, a, n)))));
}

// -----------------------------------------------------------------------------
// Conjunction, implication and disjunction.
// -----------------------------------------------------------------------------

CPPL_TEST(conjunction_goal_is_composed_from_arithmetic_facts) {
    const auto x = var(0);
    const auto premise = k::Proposition::conjunction(holds(k::PrimOp::Less, kU32, x, lit(kU32, 10)),
                                                     holds(k::PrimOp::Greater, kU32, x, lit(kU32, 0)));
    const auto increment = add(kU32, x, lit(kU32, 1));
    const auto conclusion = k::Proposition::conjunction(holds(k::PrimOp::LessEqual, kU32, increment, lit(kU32, 10)),
                                                        holds(k::PrimOp::GreaterEqual, kU32, increment, lit(kU32, 1)));
    CPPL_CHECK(proven(closed(kU32, 1, {premise}, conclusion)));
}

CPPL_TEST(missing_left_conjunct_cannot_prove_full_conclusion) {
    const auto x = var(0);
    const auto below_ten = holds(k::PrimOp::Less, kU32, x, lit(kU32, 10));
    const auto positive = holds(k::PrimOp::Greater, kU32, x, lit(kU32, 0));
    const auto increment = add(kU32, x, lit(kU32, 1));
    const auto conclusion = k::Proposition::conjunction(holds(k::PrimOp::LessEqual, kU32, increment, lit(kU32, 10)),
                                                        holds(k::PrimOp::GreaterEqual, kU32, increment, lit(kU32, 1)));
    CPPL_CHECK(not_accepted_from_automation(closed(kU32, 1, {positive}, conclusion)));
    // `below_ten` alone already entails both conjuncts over u32, so it is a
    // completeness case rather than a soundness case.
    CPPL_CHECK(proven(closed(kU32, 1, {below_ten}, conclusion)));
}

CPPL_TEST(nested_implications_compose_transitively) {
    const auto z = var(0);
    const auto y = var(1);
    const auto x = var(2);
    CPPL_CHECK(proven(closed(kI32, 3, {holds(k::PrimOp::Less, kI32, x, y), holds(k::PrimOp::Less, kI32, y, z)},
                             holds(k::PrimOp::Less, kI32, x, z))));
}

CPPL_TEST(disjunction_premise_requires_both_cases_to_support_goal) {
    const auto x = var(0);
    const auto outside = k::Proposition::disjunction(holds(k::PrimOp::LessEqual, kI32, x, lit(kI32, 0)),
                                                     holds(k::PrimOp::GreaterEqual, kI32, x, lit(kI32, 10)));
    const auto goal = closed(kI32, 1, {outside}, holds(k::PrimOp::NotEqual, kI32, x, lit(kI32, 5)));
    CPPL_CHECK(proven(goal));
}

CPPL_TEST(one_disjunction_branch_not_supporting_goal_causes_rejection) {
    const auto x = var(0);
    const auto maybe_small = k::Proposition::disjunction(holds(k::PrimOp::LessEqual, kI32, x, lit(kI32, 0)),
                                                         holds(k::PrimOp::LessEqual, kI32, x, lit(kI32, 10)));
    CPPL_CHECK(not_accepted_from_automation(
        closed(kI32, 1, {maybe_small}, holds(k::PrimOp::NotEqual, kI32, x, lit(kI32, 5)))));
}

CPPL_TEST(arithmetic_fact_can_introduce_left_disjunct) {
    const auto x = var(0);
    const auto goal = k::Proposition::disjunction(holds(k::PrimOp::Less, kI32, x, lit(kI32, 10)),
                                                  holds(k::PrimOp::Greater, kI32, x, lit(kI32, 100)));
    CPPL_CHECK(proven(closed(kI32, 1, {holds(k::PrimOp::Less, kI32, x, lit(kI32, 10))}, goal)));
}

CPPL_TEST(arithmetic_fact_can_introduce_right_disjunct) {
    const auto x = var(0);
    const auto goal = k::Proposition::disjunction(holds(k::PrimOp::Less, kI32, x, lit(kI32, -100)),
                                                  holds(k::PrimOp::Greater, kI32, x, lit(kI32, 10)));
    CPPL_CHECK(proven(closed(kI32, 1, {holds(k::PrimOp::Greater, kI32, x, lit(kI32, 10))}, goal)));
}

CPPL_TEST(total_order_trichotomy_is_proven) {
    const auto y = var(0);
    const auto x = var(1);
    const auto goal = k::Proposition::disjunction(
        holds(k::PrimOp::Less, kI8, x, y),
        k::Proposition::disjunction(equal(kI8, x, y), holds(k::PrimOp::Greater, kI8, x, y)));
    CPPL_CHECK(proven(closed(kI8, 2, {}, goal)));
}

// -----------------------------------------------------------------------------
// Quantifier and de Bruijn discipline.
// -----------------------------------------------------------------------------

CPPL_TEST(two_binders_use_innermost_var_zero) {
    const auto outer = var(1);
    const auto inner = var(0);
    CPPL_CHECK(proven(closed(kU32, 2, {equal(kU32, outer, inner)}, equal(kU32, inner, outer))));
}

CPPL_TEST(three_binder_chain_preserves_indices) {
    const auto outer = var(2);
    const auto middle = var(1);
    const auto inner = var(0);
    CPPL_CHECK(
        proven(closed(kU32, 3, {equal(kU32, outer, middle), equal(kU32, middle, inner)}, equal(kU32, outer, inner))));
}

CPPL_TEST(out_of_scope_debruijn_index_makes_goal_unprovable) {
    const auto malformed = closed(kU32, 1, {}, equal(kU32, var(1), var(1)));
    CPPL_CHECK(not_accepted_from_automation(malformed));
}

CPPL_TEST(mixed_binder_types_are_checked_exactly) {
    // forall u32 outer. forall i32 inner. inner == inner
    const auto goal = quantify({type(kU32), type(kI32)}, equal(kI32, var(0), var(0)));
    CPPL_CHECK(proven(goal));
}

CPPL_TEST(mixed_binder_type_confusion_is_rejected) {
    // Var{1} is u32, but the equality falsely claims it is i32.
    const auto goal = quantify({type(kU32), type(kI32)}, equal(kI32, var(1), var(1)));
    CPPL_CHECK(not_accepted_from_automation(goal));
}

CPPL_TEST(quantifier_order_is_semantically_significant) {
    const auto good = quantify({type(kU32), type(kI32)}, equal(kI32, var(0), var(0)));
    const auto bad = quantify({type(kI32), type(kU32)}, equal(kI32, var(0), var(0)));
    CPPL_CHECK(proven(good));
    CPPL_CHECK(not_accepted_from_automation(bad));
}

// -----------------------------------------------------------------------------
// Conditional arithmetic.
// -----------------------------------------------------------------------------

CPPL_TEST(select_of_two_bounded_constants_is_bounded_for_every_condition) {
    const auto condition = var(0);
    const auto chosen = select(kU8, condition, lit(kU8, 3), lit(kU8, 5));
    const auto goal = quantify({type(k::kBoolean)}, holds(k::PrimOp::LessEqual, kU8, chosen, lit(kU8, 5)));
    CPPL_CHECK(proven(goal));
}

CPPL_TEST(select_is_not_equal_to_one_branch_for_every_condition_when_branches_differ) {
    const auto condition = var(0);
    const auto chosen = select(kU8, condition, lit(kU8, 3), lit(kU8, 5));
    const auto goal = quantify({type(k::kBoolean)}, equal(kU8, chosen, lit(kU8, 3)));
    CPPL_CHECK(not_accepted_from_automation(goal));
}

CPPL_TEST(select_of_identical_branches_equals_that_branch) {
    // forall x:u8. forall c:bool. select(c, x, x) == x
    const auto x = var(1);
    const auto condition = var(0);
    const auto chosen = select(kU8, condition, x, x);
    const auto goal = quantify({type(kU8), type(k::kBoolean)}, equal(kU8, chosen, x));
    CPPL_CHECK(proven(goal));
}

CPPL_TEST(select_case_reasoning_preserves_common_upper_bound) {
    // forall x y c. x <= 100 -> y <= 100 -> select(c,x,y) <= 100
    const auto x = var(2);
    const auto y = var(1);
    const auto condition = var(0);
    const auto chosen = select(kU8, condition, x, y);
    auto body = imply_all(
        {holds(k::PrimOp::LessEqual, kU8, x, lit(kU8, 100)), holds(k::PrimOp::LessEqual, kU8, y, lit(kU8, 100))},
        holds(k::PrimOp::LessEqual, kU8, chosen, lit(kU8, 100)));
    CPPL_CHECK(proven(quantify({type(kU8), type(kU8), type(k::kBoolean)}, std::move(body))));
}

// -----------------------------------------------------------------------------
// Definition unfolding in arithmetic.
// -----------------------------------------------------------------------------

CPPL_TEST(total_definition_can_be_unfolded_for_reflexive_arithmetic_goal) {
    k::Context context;
    const k::DefId inc_id{1};
    const auto define_result = context.define(unary_definition(inc_id, "inc", kU8, kU8, add(kU8, var(0), lit(kU8, 1))));
    CPPL_CHECK(define_result.has_value());

    const auto x = var(0);
    const auto call = k::Term::call(inc_id, {x});
    const auto goal = closed(kU8, 1, {}, equal(kU8, call, add(kU8, x, lit(kU8, 1))));
    CPPL_CHECK(proven(context, goal));
}

CPPL_TEST(definition_chain_unfolds_deterministically) {
    k::Context context;
    const k::DefId inc_id{1};
    const k::DefId twice_id{2};
    CPPL_CHECK(context.define(unary_definition(inc_id, "inc", kU8, kU8, add(kU8, var(0), lit(kU8, 1)))).has_value());
    CPPL_CHECK(context
                   .define(unary_definition(twice_id, "twice_inc", kU8, kU8,
                                            k::Term::call(inc_id, {k::Term::call(inc_id, {var(0)})})))
                   .has_value());

    const auto x = var(0);
    const auto call = k::Term::call(twice_id, {x});
    const auto expected = add(kU8, add(kU8, x, lit(kU8, 1)), lit(kU8, 1));
    CPPL_CHECK(proven(context, closed(kU8, 1, {}, equal(kU8, call, expected))));
}

CPPL_TEST(duplicate_definition_id_is_rejected) {
    k::Context context;
    const k::DefId id{7};
    CPPL_CHECK(context.define(unary_definition(id, "first", kU8, kU8, var(0))).has_value());
    const auto second = context.define(unary_definition(id, "second", kU8, kU8, var(0)));
    CPPL_CHECK(!second.has_value());
    CPPL_CHECK_EQ(second.error().kind, k::CoreErrorKind::DuplicateDefinition);
}

CPPL_TEST(definition_call_to_unknown_id_is_rejected) {
    k::Context context;
    const auto result =
        context.define(unary_definition(k::DefId{2}, "bad", kU8, kU8, k::Term::call(k::DefId{99}, {var(0)})));
    CPPL_CHECK(!result.has_value());
    CPPL_CHECK_EQ(result.error().kind, k::CoreErrorKind::UnknownDefinition);
}

CPPL_TEST(definition_call_with_wrong_arity_is_rejected) {
    k::Context context;
    const k::DefId id{1};
    CPPL_CHECK(context.define(unary_definition(id, "id", kU8, kU8, var(0))).has_value());
    const auto term = k::Term::call(id, {});
    const auto typed = k::type_of(context, {}, term);
    CPPL_CHECK(!typed.has_value());
    CPPL_CHECK_EQ(typed.error().kind, k::CoreErrorKind::ArityMismatch);
}

CPPL_TEST(self_recursive_definition_is_rejected_by_construction) {
    k::Context context;
    const k::DefId self{1};
    const auto result = context.define(unary_definition(self, "self", kU8, kU8, k::Term::call(self, {var(0)})));
    CPPL_CHECK(!result.has_value());
    CPPL_CHECK_EQ(result.error().kind, k::CoreErrorKind::UnknownDefinition);
}
