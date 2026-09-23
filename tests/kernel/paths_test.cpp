#include "cppl/kernel/box.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace {
namespace k = cppl::kernel;
const auto u32 = k::Type::integer(32, k::Signedness::Unsigned);
const auto i32 = k::Type::integer(32, k::Signedness::Signed);

k::Term number(std::int64_t value) {
    return k::Term::literal(u32.integer_type(), value);
}
k::Term variable(std::uint32_t index = 0) {
    return k::Term::variable(k::VarIndex{index});
}
k::Term compare(k::PrimOp op, k::Term lhs, k::Term rhs) {
    return k::Term::primitive(op, u32.integer_type(), {std::move(lhs), std::move(rhs)});
}
k::Proposition bounded(k::Term value) {
    return k::predicate(compare(k::PrimOp::LessEqual, std::move(value), number(10)), true);
}

k::ProofTerm clamp_proof() {
    const auto condition = compare(k::PrimOp::LessEqual, variable(), number(10));
    return k::ProofTerm::conditional_elimination(
        u32, condition, variable(), number(10), bounded(variable()),
        k::ProofTerm::implication_introduction(k::predicate(condition, true),
                                               k::ProofTerm::hypothesis(k::HypothesisIndex{0})),
        k::ProofTerm::implication_introduction(k::predicate(condition, false), k::ProofTerm::reflexivity()));
}

k::Proposition clamp_goal() {
    return k::Proposition::for_all(
        u32,
        bounded(k::Term::primitive(k::PrimOp::Select, u32.integer_type(),
                                   {compare(k::PrimOp::LessEqual, variable(), number(10)), variable(), number(10)})));
}

k::ConditionalElimination& branch(k::ProofTerm& proof) {
    return std::get<k::ConditionalElimination>(proof.node);
}
k::CheckResult check_clamp(k::ProofTerm proof, k::CoreLimits limits = {}) {
    return k::check({}, clamp_goal(), k::ProofTerm::forall_introduction(u32, std::move(proof)), limits);
}
} // namespace

CPPL_TEST(conditional_elimination_checks_both_paths_and_exports_no_hypotheses) {
    CPPL_CHECK(check_clamp(clamp_proof()).has_value());
    CPPL_CHECK(!k::check({}, clamp_goal(), k::ProofTerm::hypothesis(k::HypothesisIndex{0}), {}));
}

CPPL_TEST(conditional_elimination_rejects_forged_arms_premises_and_motives) {
    auto evidence = clamp_proof();
    branch(evidence).when_false = number(11);
    CPPL_CHECK(!check_clamp(evidence));
    evidence = clamp_proof();
    branch(evidence).false_case = branch(evidence).true_case;
    CPPL_CHECK(!check_clamp(evidence));
    evidence = clamp_proof();
    branch(evidence).condition = compare(k::PrimOp::Greater, variable(), number(10));
    CPPL_CHECK(!check_clamp(evidence));
    evidence = clamp_proof();
    branch(evidence).motive = k::Box<k::Proposition>{bounded(variable(1))};
    CPPL_CHECK(!check_clamp(evidence));
    evidence = clamp_proof();
    branch(evidence).true_case = k::Box<k::ProofTerm>{k::ProofTerm::reflexivity()};
    CPPL_CHECK(!check_clamp(evidence));
}

CPPL_TEST(conditional_elimination_rejects_ill_typed_terms_and_captured_variables) {
    auto evidence = clamp_proof();
    branch(evidence).condition = number(1);
    CPPL_CHECK(!check_clamp(evidence));
    evidence = clamp_proof();
    branch(evidence).when_true = k::Term::literal(i32.integer_type(), 1);
    CPPL_CHECK(!check_clamp(evidence));
    evidence = clamp_proof();
    branch(evidence).motive = k::Box<k::Proposition>{bounded(variable(2))};
    CPPL_CHECK(!check_clamp(evidence));
    auto limits = k::CoreLimits{};
    limits.max_term_depth = 1;
    CPPL_CHECK(!check_clamp(clamp_proof(), limits));
}

CPPL_TEST(comparison_primitives_compute_exact_signed_and_unsigned_results) {
    const k::PrimOp operators[] = {k::PrimOp::Equal,     k::PrimOp::NotEqual, k::PrimOp::Less,
                                   k::PrimOp::LessEqual, k::PrimOp::Greater,  k::PrimOp::GreaterEqual};
    const bool results[] = {false, true, true, true, false, false};
    for (std::size_t index = 0; index < 6; ++index) {
        for (const auto& type : {u32, i32}) {
            const auto lhs = k::Term::literal(type.integer_type(), type == i32 ? -5 : 5);
            const auto rhs = k::Term::literal(type.integer_type(), 10);
            const auto condition = k::Term::primitive(operators[index], type.integer_type(), {lhs, rhs});
            const auto goal = k::predicate(condition, results[index]);
            CPPL_CHECK(k::check({}, goal, k::ProofTerm::reflexivity(), {}).has_value());
            CPPL_CHECK(!k::check({}, k::predicate(condition, !results[index]), k::ProofTerm::reflexivity(), {}));
        }
    }
    const auto u64 = k::Type::integer(64, k::Signedness::Unsigned);
    const auto max_signed = k::Term::literal(u64.integer_type(), std::numeric_limits<std::int64_t>::max());
    const auto high = k::Term::primitive(k::PrimOp::AddWrap, u64.integer_type(),
                                         {max_signed, k::Term::literal(u64.integer_type(), 1)});
    const auto larger = k::Term::primitive(k::PrimOp::Greater, u64.integer_type(), {high, max_signed});
    CPPL_CHECK(k::check({}, k::predicate(larger, true), k::ProofTerm::reflexivity(), {}).has_value());
}

CPPL_TEST(comparisons_and_selection_reject_malformed_core_terms) {
    const auto boolean = k::Term::literal(k::kBoolean, 1);
    // The unknown opcode is one of the malformed terms under test; the kernel
    // must reject it rather than guess at an operation.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto unknown_opcode = k::Term::primitive(static_cast<k::PrimOp>(255), u32.integer_type(), {});
    for (const auto& malformed :
         {unknown_opcode, compare(k::PrimOp::Less, number(1), k::Term::literal(i32.integer_type(), 1)),
          k::Term::primitive(k::PrimOp::Less, u32.integer_type(), {number(1)}),
          k::Term::primitive(k::PrimOp::Select, u32.integer_type(), {number(1), number(1), number(2)}),
          k::Term::primitive(k::PrimOp::Select, u32.integer_type(), {boolean, number(1), boolean}),
          k::Term::primitive(k::PrimOp::Not, u32.integer_type(), {number(1)})}) {
        CPPL_CHECK(!k::type_of({}, {}, malformed, {}));
        const auto forged = k::Proposition::equality(u32, malformed, malformed);
        CPPL_CHECK(!k::check({}, forged, k::ProofTerm::reflexivity(), {}));
    }
}

CPPL_TEST(negated_comparisons_preserve_equality_and_boolean_polarity) {
    const auto equal = compare(k::PrimOp::Equal, variable(), number(0));
    const auto unequal = compare(k::PrimOp::NotEqual, variable(), number(0));
    const auto negated = k::Term::primitive(k::PrimOp::Not, k::kBoolean, {equal});
    CPPL_CHECK(k::predicate(equal, false) == k::predicate(unequal, true));
    CPPL_CHECK(k::predicate(equal, false) == k::predicate(negated, true));
    CPPL_CHECK(k::predicate(unequal, false) == k::Proposition::equality(u32, variable(), number(0)));
    const auto selected = k::Term::primitive(k::PrimOp::Select, u32.integer_type(),
                                             {k::Term::literal(k::kBoolean, 0), number(1), number(2)});
    CPPL_CHECK(
        k::check({}, k::Proposition::equality(u32, selected, number(2)), k::ProofTerm::reflexivity(), {}).has_value());
}

CPPL_TEST(path_rule_does_not_introduce_order_weakening) {
    const auto strict = compare(k::PrimOp::Less, variable(), number(10));
    const auto goal =
        k::Proposition::for_all(u32, k::Proposition::implication(k::predicate(strict, true), bounded(variable())));
    const auto proof = k::ProofTerm::forall_introduction(
        u32, k::ProofTerm::implication_introduction(k::predicate(strict, true),
                                                    k::ProofTerm::hypothesis(k::HypothesisIndex{0})));
    CPPL_CHECK(!k::check({}, goal, proof, {}));
}
