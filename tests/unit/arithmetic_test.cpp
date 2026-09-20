// Arithmetic evidence produced by automation. Whatever it proposes is put to
// the kernel here; a goal counts as proven only when the kernel accepts.

#include "cppl/automation/evidence.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/testing/test.hpp"

#include <ranges>

namespace {

namespace k = cppl::kernel;

const k::IntType kU32{32, k::Signedness::Unsigned};
const k::IntType kU64{64, k::Signedness::Unsigned};
const k::IntType kI32{32, k::Signedness::Signed};

k::Term var(std::uint32_t index) {
    return k::Term::variable(k::VarIndex{index});
}
k::Term lit(const k::IntType& type, std::int64_t value) {
    return k::Term::literal(type, value);
}
k::Term add(const k::IntType& type, k::Term a, k::Term b) {
    return k::Term::primitive(k::PrimOp::AddWrap, type, {std::move(a), std::move(b)});
}
k::Term mul(const k::IntType& type, k::Term a, k::Term b) {
    return k::Term::primitive(k::PrimOp::MulWrap, type, {std::move(a), std::move(b)});
}
k::Proposition holds(k::PrimOp op, const k::IntType& type, k::Term a, k::Term b, bool value = true) {
    return k::Proposition::equality(k::Type{k::kBoolean}, k::Term::primitive(op, type, {std::move(a), std::move(b)}),
                                    k::Term::literal(k::kBoolean, value ? 1 : 0));
}
k::Proposition equal(const k::IntType& type, k::Term a, k::Term b) {
    return k::Proposition::equality(k::Type{type}, std::move(a), std::move(b));
}

// forall binders. premises -> conclusion
k::Proposition closed(const k::IntType& type, std::size_t binders, std::vector<k::Proposition> premises,
                      k::Proposition conclusion) {
    for (auto& premise : std::views::reverse(premises)) {
        conclusion = k::Proposition::implication(std::move(premise), std::move(conclusion));
    }
    for (std::size_t index = 0; index < binders; ++index) {
        conclusion = k::Proposition::for_all(k::Type{type}, std::move(conclusion));
    }
    return conclusion;
}

bool proven(const k::Proposition& goal) {
    const k::Context context;
    const auto evidence = cppl::automation::propose(context, goal);
    return evidence.has_value() && k::check(context, goal, evidence->proof, {}).has_value();
}

} // namespace

CPPL_TEST(a_loop_step_preserves_its_bound) {
    // forall n i. i <= n -> i < n -> i + 1 <= n
    const auto n = var(1);
    const auto i = var(0);
    CPPL_CHECK(proven(closed(kU32, 2, {holds(k::PrimOp::LessEqual, kU32, i, n), holds(k::PrimOp::Less, kU32, i, n)},
                             holds(k::PrimOp::LessEqual, kU32, add(kU32, i, lit(kU32, 1)), n))));
    // Without i < n the step can pass n, and at the maximum it wraps.
    CPPL_CHECK(!proven(closed(kU32, 2, {holds(k::PrimOp::LessEqual, kU32, i, n)},
                              holds(k::PrimOp::LessEqual, kU32, add(kU32, i, lit(kU32, 1)), n))));
}

CPPL_TEST(a_loop_exit_pins_its_counter) {
    // forall n i. i <= n -> !(i < n) -> i == n
    const auto n = var(1);
    const auto i = var(0);
    CPPL_CHECK(
        proven(closed(kU32, 2, {holds(k::PrimOp::LessEqual, kU32, i, n), holds(k::PrimOp::Less, kU32, i, n, false)},
                      equal(kU32, i, n))));
    CPPL_CHECK(!proven(closed(kU32, 2, {holds(k::PrimOp::Less, kU32, i, n, false)}, equal(kU32, i, n))));
}

CPPL_TEST(conjunctions_compose_with_checked_machine_arithmetic) {
    const auto x = var(0);
    const auto below_ten = holds(k::PrimOp::Less, kU32, x, lit(kU32, 10));
    const auto positive = holds(k::PrimOp::Greater, kU32, x, lit(kU32, 0));
    const auto premise = k::Proposition::conjunction(below_ten, positive);
    const auto increment = add(kU32, x, lit(kU32, 1));
    const auto conclusion = k::Proposition::conjunction(holds(k::PrimOp::LessEqual, kU32, increment, lit(kU32, 10)),
                                                        holds(k::PrimOp::Greater, kU32, increment, lit(kU32, 1)));
    CPPL_CHECK(proven(closed(kU32, 1, {premise}, conclusion)));
    // Either missing premise makes one conjunct false, including at wraparound.
    CPPL_CHECK(!proven(closed(kU32, 1, {below_ten}, conclusion)));
    CPPL_CHECK(!proven(closed(kU32, 1, {positive}, conclusion)));
}

CPPL_TEST(an_accumulator_follows_its_counter_to_the_exit) {
    // forall a n i r. r == a * i -> i <= n -> !(i < n) -> r == a * n. The
    // counter's equality comes from arithmetic; the product needs rewriting.
    const auto a = var(3);
    const auto n = var(2);
    const auto i = var(1);
    const auto r = var(0);
    CPPL_CHECK(proven(closed(kU32, 4,
                             {equal(kU32, r, mul(kU32, a, i)), holds(k::PrimOp::LessEqual, kU32, i, n),
                              holds(k::PrimOp::Less, kU32, i, n, false)},
                             equal(kU32, r, mul(kU32, a, n)))));
    CPPL_CHECK(!proven(closed(kU32, 4, {equal(kU32, r, mul(kU32, a, i)), holds(k::PrimOp::LessEqual, kU32, i, n)},
                              equal(kU32, r, mul(kU32, a, n)))));
}

CPPL_TEST(wrapping_is_respected_at_every_width) {
    const auto x = var(0);
    // x + 1 > x fails at the maximum; below a bound it holds.
    CPPL_CHECK(!proven(closed(kU32, 1, {}, holds(k::PrimOp::Greater, kU32, add(kU32, x, lit(kU32, 1)), x))));
    CPPL_CHECK(proven(closed(kU32, 1, {holds(k::PrimOp::Less, kU32, x, lit(kU32, 10))},
                             holds(k::PrimOp::Greater, kU32, add(kU32, x, lit(kU32, 1)), x))));
    CPPL_CHECK(proven(closed(kU64, 1, {holds(k::PrimOp::Less, kU64, x, lit(kU64, 10))},
                             holds(k::PrimOp::Less, kU64, add(kU64, x, lit(kU64, 1)), lit(kU64, 11)))));
    CPPL_CHECK(!proven(closed(kU64, 1, {}, holds(k::PrimOp::Less, kU64, add(kU64, x, lit(kU64, 1)), lit(kU64, 11)))));
}

CPPL_TEST(signed_order_is_reasoned_about_without_arithmetic) {
    const auto x = var(0);
    CPPL_CHECK(proven(closed(kI32, 1, {holds(k::PrimOp::LessEqual, kI32, x, lit(kI32, 10))},
                             holds(k::PrimOp::Less, kI32, x, lit(kI32, 20)))));
    CPPL_CHECK(!proven(closed(kI32, 1, {holds(k::PrimOp::LessEqual, kI32, x, lit(kI32, 10))},
                              holds(k::PrimOp::GreaterEqual, kI32, x, lit(kI32, 0)))));
}

CPPL_TEST(contradictory_premises_prove_any_arithmetic_goal_and_nothing_else_does) {
    const auto x = var(1);
    const auto y = var(0);
    CPPL_CHECK(proven(closed(kU32, 2, {holds(k::PrimOp::Less, kU32, x, y), holds(k::PrimOp::Less, kU32, y, x)},
                             equal(kU32, x, lit(kU32, 7)))));
    CPPL_CHECK(
        !proven(closed(kU32, 2, {holds(k::PrimOp::LessEqual, kU32, x, y), holds(k::PrimOp::LessEqual, kU32, y, x)},
                       equal(kU32, x, lit(kU32, 7)))));
}

CPPL_TEST(evidence_for_one_goal_is_not_evidence_for_a_stronger_one) {
    const auto n = var(1);
    const auto i = var(0);
    const auto preserved =
        closed(kU32, 2, {holds(k::PrimOp::LessEqual, kU32, i, n), holds(k::PrimOp::Less, kU32, i, n)},
               holds(k::PrimOp::LessEqual, kU32, add(kU32, i, lit(kU32, 1)), n));
    const auto stronger = closed(kU32, 2, {holds(k::PrimOp::LessEqual, kU32, i, n), holds(k::PrimOp::Less, kU32, i, n)},
                                 holds(k::PrimOp::Less, kU32, add(kU32, i, lit(kU32, 1)), n));
    const k::Context context;
    const auto evidence = cppl::automation::propose(context, preserved);
    CPPL_CHECK(evidence.has_value());
    CPPL_CHECK(k::check(context, preserved, evidence->proof, {}).has_value());
    CPPL_CHECK(!k::check(context, stronger, evidence->proof, {}).has_value());
    CPPL_CHECK(!proven(stronger));
}
