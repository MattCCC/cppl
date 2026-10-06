// Arithmetic evidence tests (unit_arithmetic_test): realistic verification
// obligations, stress, and width, certificate and API pressure.

#include "arithmetic_test_support.hpp"
#include "cppl/kernel/box.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace arithmetic_test_detail;

// -----------------------------------------------------------------------------
// Realistic verification obligations.
// -----------------------------------------------------------------------------

CPPL_TEST(bounded_counter_preservation_for_constant_limit) {
    const auto i = var(0);
    const auto goal =
        closed(kU32, 1,
               {holds(k::PrimOp::LessEqual, kU32, i, lit(kU32, 100)), holds(k::PrimOp::Less, kU32, i, lit(kU32, 100))},
               holds(k::PrimOp::LessEqual, kU32, add(kU32, i, lit(kU32, 1)), lit(kU32, 100)));
    CPPL_CHECK(proven(goal));
}

CPPL_TEST(bounded_counter_preservation_detects_off_by_one_bug) {
    const auto i = var(0);
    const auto goal =
        closed(kU32, 1,
               {holds(k::PrimOp::LessEqual, kU32, i, lit(kU32, 100)), holds(k::PrimOp::Less, kU32, i, lit(kU32, 100))},
               holds(k::PrimOp::Less, kU32, add(kU32, i, lit(kU32, 1)), lit(kU32, 100)));
    CPPL_CHECK(not_accepted_from_automation(goal));
}

CPPL_TEST(two_step_counter_preservation_needs_two_slots_of_headroom) {
    const auto i = var(0);
    const auto i2 = add(kU32, add(kU32, i, lit(kU32, 1)), lit(kU32, 1));
    const auto goal = closed(kU32, 1, {holds(k::PrimOp::LessEqual, kU32, i, lit(kU32, 98))},
                             holds(k::PrimOp::LessEqual, kU32, i2, lit(kU32, 100)));
    CPPL_CHECK(proven(goal));
}

CPPL_TEST(two_step_counter_without_headroom_is_not_safe) {
    const auto i = var(0);
    const auto i2 = add(kU32, add(kU32, i, lit(kU32, 1)), lit(kU32, 1));
    const auto goal = closed(kU32, 1, {holds(k::PrimOp::LessEqual, kU32, i, lit(kU32, 99))},
                             holds(k::PrimOp::LessEqual, kU32, i2, lit(kU32, 100)));
    CPPL_CHECK(not_accepted_from_automation(goal));
}

CPPL_TEST(range_checked_percentage_increment_stays_in_range) {
    const auto p = var(0);
    const auto goal = closed(kU8, 1, {holds(k::PrimOp::Less, kU8, p, lit(kU8, 100))},
                             holds(k::PrimOp::LessEqual, kU8, add(kU8, p, lit(kU8, 1)), lit(kU8, 100)));
    CPPL_CHECK(proven(goal));
}

CPPL_TEST(range_checked_percentage_increment_without_strict_bound_is_rejected) {
    const auto p = var(0);
    const auto goal = closed(kU8, 1, {holds(k::PrimOp::LessEqual, kU8, p, lit(kU8, 100))},
                             holds(k::PrimOp::LessEqual, kU8, add(kU8, p, lit(kU8, 1)), lit(kU8, 100)));
    CPPL_CHECK(not_accepted_from_automation(goal));
}

CPPL_TEST(index_plus_one_is_within_length_when_index_is_strictly_below_length) {
    const auto length = var(1);
    const auto index = var(0);
    const auto goal = closed(kU32, 2, {holds(k::PrimOp::Less, kU32, index, length)},
                             holds(k::PrimOp::LessEqual, kU32, add(kU32, index, lit(kU32, 1)), length));
    CPPL_CHECK(proven(goal));
}

CPPL_TEST(index_plus_one_is_not_strictly_within_length_at_last_element) {
    const auto length = var(1);
    const auto index = var(0);
    const auto goal = closed(kU32, 2, {holds(k::PrimOp::Less, kU32, index, length)},
                             holds(k::PrimOp::Less, kU32, add(kU32, index, lit(kU32, 1)), length));
    CPPL_CHECK(not_accepted_from_automation(goal));
}

CPPL_TEST(nonnegative_signed_value_plus_one_stays_positive_when_below_max) {
    const auto x = var(0);
    const auto goal = closed(kI32, 1,
                             {holds(k::PrimOp::GreaterEqual, kI32, x, lit(kI32, 0)),
                              holds(k::PrimOp::Less, kI32, x, lit(kI32, std::numeric_limits<std::int32_t>::max()))},
                             holds(k::PrimOp::Greater, kI32, add(kI32, x, lit(kI32, 1)), lit(kI32, 0)));
    CPPL_CHECK(proven(goal));
}

CPPL_TEST(signed_increment_without_max_guard_cannot_prove_positivity) {
    const auto x = var(0);
    const auto goal = closed(kI32, 1, {holds(k::PrimOp::GreaterEqual, kI32, x, lit(kI32, 0))},
                             holds(k::PrimOp::Greater, kI32, add(kI32, x, lit(kI32, 1)), lit(kI32, 0)));
    CPPL_CHECK(not_accepted_from_automation(goal));
}

// -----------------------------------------------------------------------------
// Stress: deeper linear systems and repeated use of the same facts.
// -----------------------------------------------------------------------------

CPPL_TEST(long_linear_chain_is_proven) {
    const auto e = var(0);
    const auto d = var(1);
    const auto c = var(2);
    const auto b = var(3);
    const auto a = var(4);
    CPPL_CHECK(proven(closed(kI32, 5,
                             {holds(k::PrimOp::LessEqual, kI32, a, b), holds(k::PrimOp::LessEqual, kI32, b, c),
                              holds(k::PrimOp::LessEqual, kI32, c, d), holds(k::PrimOp::LessEqual, kI32, d, e)},
                             holds(k::PrimOp::LessEqual, kI32, a, e))));
}

CPPL_TEST(long_strict_chain_is_proven) {
    const auto e = var(0);
    const auto d = var(1);
    const auto c = var(2);
    const auto b = var(3);
    const auto a = var(4);
    CPPL_CHECK(proven(closed(kU16, 5,
                             {holds(k::PrimOp::Less, kU16, a, b), holds(k::PrimOp::Less, kU16, b, c),
                              holds(k::PrimOp::Less, kU16, c, d), holds(k::PrimOp::Less, kU16, d, e)},
                             holds(k::PrimOp::Less, kU16, a, e))));
}

CPPL_TEST(repeated_identical_premises_do_not_change_semantics) {
    const auto x = var(0);
    const auto p = holds(k::PrimOp::Less, kU32, x, lit(kU32, 10));
    CPPL_CHECK(proven(closed(kU32, 1, {p, p, p, p}, holds(k::PrimOp::LessEqual, kU32, x, lit(kU32, 10)))));
}

CPPL_TEST(reordering_independent_premises_does_not_change_result) {
    const auto x = var(0);
    const auto p = holds(k::PrimOp::Less, kU32, x, lit(kU32, 10));
    const auto q = holds(k::PrimOp::Greater, kU32, x, lit(kU32, 0));
    const auto goal1 = closed(kU32, 1, {p, q}, holds(k::PrimOp::LessEqual, kU32, x, lit(kU32, 10)));
    const auto goal2 = closed(kU32, 1, {q, p}, holds(k::PrimOp::LessEqual, kU32, x, lit(kU32, 10)));
    CPPL_CHECK(proven(goal1));
    CPPL_CHECK(proven(goal2));
}

CPPL_TEST(contradiction_remains_sound_under_many_irrelevant_facts) {
    const auto x = var(0);
    CPPL_CHECK(proven(closed(
        kI32, 1,
        {holds(k::PrimOp::GreaterEqual, kI32, x, lit(kI32, -100)), holds(k::PrimOp::LessEqual, kI32, x, lit(kI32, 100)),
         holds(k::PrimOp::Less, kI32, x, lit(kI32, 0)), holds(k::PrimOp::GreaterEqual, kI32, x, lit(kI32, 0))},
        equal(kI32, x, lit(kI32, 42)))));
}

CPPL_TEST(noncontradictory_many_fact_system_does_not_prove_arbitrary_equality) {
    const auto x = var(0);
    CPPL_CHECK(not_accepted_from_automation(closed(kI32, 1,
                                                   {holds(k::PrimOp::GreaterEqual, kI32, x, lit(kI32, -100)),
                                                    holds(k::PrimOp::LessEqual, kI32, x, lit(kI32, 100)),
                                                    holds(k::PrimOp::GreaterEqual, kI32, x, lit(kI32, 0))},
                                                   equal(kI32, x, lit(kI32, 42)))));
}

// -----------------------------------------------------------------------------
// Additional width, certificate, and API-pressure cases.
// -----------------------------------------------------------------------------

CPPL_TEST(unsigned_64_literal_minus_one_is_not_silently_reinterpreted_as_maximum) {
    const k::Context context;
    const auto typed = k::type_of(context, {}, lit(kU64, -1));
    CPPL_CHECK(!typed.has_value());
    CPPL_CHECK_EQ(typed.error().kind, k::CoreErrorKind::MalformedLiteral);
}

CPPL_TEST(unsigned_64_normalization_can_represent_two_to_the_63) {
    // Deliberately red against an int64_t-valued Literal representation.
    const auto term = add(kU64, lit(kU64, std::numeric_limits<std::int64_t>::max()), lit(kU64, 1));
    const auto value = normalized_literal(term);
    CPPL_CHECK(value.has_value());
    CPPL_CHECK(static_cast<k::Wide>(*value) == pow2(63));
}

CPPL_TEST(unsigned_63_increment_is_monotone_below_representable_maximum) {
    const auto x = var(0);
    const auto max = lit(kU63, std::numeric_limits<std::int64_t>::max());
    CPPL_CHECK(proven(closed(kU63, 1, {holds(k::PrimOp::Less, kU63, x, max)},
                             holds(k::PrimOp::Greater, kU63, add(kU63, x, lit(kU63, 1)), x))));
}

CPPL_TEST(signed_64_increment_is_monotone_below_maximum) {
    const auto x = var(0);
    const auto max = lit(kI64, std::numeric_limits<std::int64_t>::max());
    CPPL_CHECK(proven(closed(kI64, 1, {holds(k::PrimOp::Less, kI64, x, max)},
                             holds(k::PrimOp::Greater, kI64, add(kI64, x, lit(kI64, 1)), x))));
}

CPPL_TEST(signed_64_increment_is_not_globally_monotone) {
    const auto x = var(0);
    CPPL_CHECK(not_accepted_from_automation(
        closed(kI64, 1, {}, holds(k::PrimOp::Greater, kI64, add(kI64, x, lit(kI64, 1)), x))));
}

CPPL_TEST(unsigned_64_add_zero_identity_remains_provable) {
    const auto x = var(0);
    CPPL_CHECK(proven(closed(kU64, 1, {}, equal(kU64, add(kU64, x, lit(kU64, 0)), x))));
}

CPPL_TEST(negative_farkas_multiplier_is_rejected) {
    const auto fact_goal = equal(kU8, lit(kU8, 1), lit(kU8, 1));
    k::ArithmeticFact fact{fact_goal, k::Box<k::ProofTerm>{k::ProofTerm::reflexivity()}};
    k::ArithmeticCertificate certificate{k::FarkasSum{{std::pair<std::uint32_t, k::Wide>{0u, k::Wide{-1}}}}};
    const auto proof = k::ProofTerm::linear_arithmetic({std::move(fact)}, std::move(certificate));
    const auto goal = equal(kU8, lit(kU8, 7), lit(kU8, 9));
    const k::Context context;
    CPPL_CHECK(!k::check(context, goal, proof, {}).has_value());
}

CPPL_TEST(farkas_constraint_index_must_be_in_range_even_with_zero_multiplier) {
    k::ArithmeticCertificate certificate{k::FarkasSum{{std::pair<std::uint32_t, k::Wide>{999999u, k::Wide{0}}}}};
    const auto proof = k::ProofTerm::linear_arithmetic({}, std::move(certificate));
    const auto goal = equal(kU8, lit(kU8, 1), lit(kU8, 2));
    const k::Context context;
    CPPL_CHECK(!k::check(context, goal, proof, {}).has_value());
}

CPPL_TEST(arithmetic_fact_restatement_must_match_its_evidence) {
    const auto claimed = equal(kU8, lit(kU8, 1), lit(kU8, 2));
    k::ArithmeticFact fact{claimed, k::Box<k::ProofTerm>{k::ProofTerm::reflexivity()}};
    const auto proof = k::ProofTerm::linear_arithmetic({std::move(fact)}, empty_farkas());
    const auto goal = equal(kU8, lit(kU8, 3), lit(kU8, 4));
    const k::Context context;
    CPPL_CHECK(!k::check(context, goal, proof, {}).has_value());
}

CPPL_TEST(machine_wrap_constant_fact_is_proven_by_kernel_checked_automation) {
    const auto goal = equal(kU8, add(kU8, lit(kU8, 255), lit(kU8, 1)), lit(kU8, 0));
    CPPL_CHECK(proven(goal));
}

CPPL_TEST(signed_machine_wrap_constant_fact_is_proven_by_kernel_checked_automation) {
    const auto goal = equal(kI8, add(kI8, lit(kI8, 127), lit(kI8, 1)), lit(kI8, -128));
    CPPL_CHECK(proven(goal));
}

CPPL_TEST(machine_wrap_does_not_justify_unbounded_integer_monotonicity) {
    const auto x = var(0);
    const auto goal = closed(kU8, 1, {}, holds(k::PrimOp::GreaterEqual, kU8, add(kU8, x, lit(kU8, 1)), x));
    CPPL_CHECK(not_accepted_from_automation(goal));
}

CPPL_TEST(equalities_can_be_chained_across_three_machine_terms) {
    const auto z = var(0);
    const auto y = var(1);
    const auto x = var(2);
    CPPL_CHECK(proven(closed(kU16, 3, {equal(kU16, x, y), equal(kU16, y, z)}, equal(kU16, x, z))));
}

CPPL_TEST(one_broken_equality_in_chain_prevents_arbitrary_conclusion) {
    const auto z = var(0);
    const auto y = var(1);
    const auto x = var(2);
    CPPL_CHECK(not_accepted_from_automation(closed(kU16, 3, {equal(kU16, x, y)}, equal(kU16, x, z))));
}

CPPL_TEST(non_strict_bound_plus_disequality_implies_strict_bound) {
    const auto y = var(0);
    const auto x = var(1);
    CPPL_CHECK(proven(closed(kI32, 2, {holds(k::PrimOp::LessEqual, kI32, x, y), holds(k::PrimOp::NotEqual, kI32, x, y)},
                             holds(k::PrimOp::Less, kI32, x, y))));
}

CPPL_TEST(strict_bound_implies_disequality) {
    const auto y = var(0);
    const auto x = var(1);
    CPPL_CHECK(proven(closed(kI32, 2, {holds(k::PrimOp::Less, kI32, x, y)}, holds(k::PrimOp::NotEqual, kI32, x, y))));
}

CPPL_TEST(disequality_alone_does_not_choose_an_order_direction) {
    const auto y = var(0);
    const auto x = var(1);
    CPPL_CHECK(not_accepted_from_automation(
        closed(kI32, 2, {holds(k::PrimOp::NotEqual, kI32, x, y)}, holds(k::PrimOp::Less, kI32, x, y))));
}
