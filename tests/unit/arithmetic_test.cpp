// Production conformance tests for cppl::automation arithmetic evidence.
//
// Normative source of truth: the production-ready C++L MAIN SPEC.
// The current public headers are treated as an implementation surface, not as
// authority over the language. Some tests in this file are intentionally
// expected to fail until the implementation catches up with the specification.
//
// Trust invariant under test:
//   automation may propose evidence;
//   only kernel::check may establish a proposition;
//   evidence for one proposition must never establish a different proposition.
//
// The suite deliberately mixes positive completeness tests with adversarial
// soundness tests. A false theorem becoming accepted is always a critical bug.

#include "cppl/automation/evidence.hpp"
#include "cppl/kernel/box.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace k = cppl::kernel;

const k::IntType kU1{1, k::Signedness::Unsigned};
const k::IntType kI1{1, k::Signedness::Signed};
const k::IntType kU2{2, k::Signedness::Unsigned};
const k::IntType kI2{2, k::Signedness::Signed};
const k::IntType kU8{8, k::Signedness::Unsigned};
const k::IntType kI8{8, k::Signedness::Signed};
const k::IntType kU16{16, k::Signedness::Unsigned};
const k::IntType kI16{16, k::Signedness::Signed};
const k::IntType kU32{32, k::Signedness::Unsigned};
const k::IntType kI32{32, k::Signedness::Signed};
const k::IntType kU63{63, k::Signedness::Unsigned};
const k::IntType kI63{63, k::Signedness::Signed};
const k::IntType kU64{64, k::Signedness::Unsigned};
const k::IntType kI64{64, k::Signedness::Signed};

k::Type type(const k::IntType& integer) {
    return k::Type{integer};
}

k::Term var(std::uint32_t index) {
    return k::Term::variable(k::VarIndex{index});
}

k::Term lit(const k::IntType& integer, std::int64_t value) {
    return k::Term::literal(integer, value);
}

k::Term unary(k::PrimOp op, const k::IntType& integer, k::Term a) {
    return k::Term::primitive(op, integer, {std::move(a)});
}

k::Term binary(k::PrimOp op, const k::IntType& integer, k::Term a, k::Term b) {
    return k::Term::primitive(op, integer, {std::move(a), std::move(b)});
}

k::Term add(const k::IntType& integer, k::Term a, k::Term b) {
    return binary(k::PrimOp::AddWrap, integer, std::move(a), std::move(b));
}

k::Term sub(const k::IntType& integer, k::Term a, k::Term b) {
    return binary(k::PrimOp::SubWrap, integer, std::move(a), std::move(b));
}

k::Term mul(const k::IntType& integer, k::Term a, k::Term b) {
    return binary(k::PrimOp::MulWrap, integer, std::move(a), std::move(b));
}

k::Term select(const k::IntType& result_type, k::Term condition, k::Term when_true, k::Term when_false) {
    return k::Term::primitive(k::PrimOp::Select, result_type,
                              {std::move(condition), std::move(when_true), std::move(when_false)});
}

k::Term compare(k::PrimOp op, const k::IntType& integer, k::Term a, k::Term b) {
    return binary(op, integer, std::move(a), std::move(b));
}

k::Proposition holds(k::PrimOp op, const k::IntType& integer, k::Term a, k::Term b, bool value = true) {
    return k::Proposition::equality(type(k::kBoolean), compare(op, integer, std::move(a), std::move(b)),
                                    lit(k::kBoolean, value ? 1 : 0));
}

k::Proposition equal(const k::IntType& integer, k::Term a, k::Term b) {
    return k::Proposition::equality(type(integer), std::move(a), std::move(b));
}

k::Proposition imply_all(std::vector<k::Proposition> premises, k::Proposition conclusion) {
    for (auto& premise : std::views::reverse(premises)) {
        conclusion = k::Proposition::implication(premise, std::move(conclusion));
    }
    return conclusion;
}

// `binders` are outermost-first. Var{0} denotes binders.back().
k::Proposition quantify(std::vector<k::Type> binders, k::Proposition body) {
    for (auto& binder : std::views::reverse(binders)) {
        body = k::Proposition::for_all(binder, std::move(body));
    }
    return body;
}

k::Proposition closed(const k::IntType& integer, std::size_t binders, std::vector<k::Proposition> premises,
                      k::Proposition conclusion) {
    conclusion = imply_all(std::move(premises), std::move(conclusion));
    for (std::size_t index = 0; index < binders; ++index) {
        conclusion = k::Proposition::for_all(type(integer), std::move(conclusion));
    }
    return conclusion;
}

std::optional<cppl::automation::Evidence> proposal(const k::Context& context, const k::Proposition& goal) {
    return cppl::automation::propose(context, goal);
}

bool accepted(const k::Context& context, const k::Proposition& goal, const k::ProofTerm& proof,
              const k::CoreLimits& limits = {}) {
    return k::check(context, goal, proof, limits).has_value();
}

bool proven(const k::Context& context, const k::Proposition& goal) {
    const auto evidence = proposal(context, goal);
    return evidence.has_value() && accepted(context, goal, evidence->proof);
}

bool proven(const k::Proposition& goal) {
    const k::Context context;
    return proven(context, goal);
}

// A sound automation layer is allowed to return no evidence for a false goal,
// but if it returns anything, the kernel must reject it.
bool not_accepted_from_automation(const k::Context& context, const k::Proposition& goal) {
    const auto evidence = proposal(context, goal);
    return !evidence.has_value() || !accepted(context, goal, evidence->proof);
}

bool not_accepted_from_automation(const k::Proposition& goal) {
    const k::Context context;
    return not_accepted_from_automation(context, goal);
}

std::optional<k::Wide> normalized_literal(const k::Context& context, const k::Term& term,
                                          const k::CoreLimits& limits = {}) {
    const auto normalized = k::normalize(context, term, limits);
    if (!normalized.has_value()) {
        return std::nullopt;
    }
    const auto* literal = std::get_if<k::Literal>(&normalized->node);
    if (literal == nullptr) {
        return std::nullopt;
    }
    return literal->value;
}

std::optional<k::Wide> normalized_literal(const k::Term& term) {
    const k::Context context;
    return normalized_literal(context, term);
}

k::ArithmeticCertificate empty_farkas() {
    return k::ArithmeticCertificate{k::FarkasSum{}};
}

k::ArithmeticCertificate impossible_multiplier_certificate() {
    return k::ArithmeticCertificate{k::FarkasSum{{std::pair<std::uint32_t, k::Wide>{999999u, k::Wide{1}}}}};
}

k::Definition unary_definition(k::DefId id, std::string name, const k::IntType& argument_type,
                               const k::IntType& result_type, k::Term body) {
    return k::Definition{id, std::move(name), {type(argument_type)}, type(result_type), std::move(body)};
}

k::Wide pow2(unsigned width) {
    return k::Wide{1} << width;
}

} // namespace

// -----------------------------------------------------------------------------
// Machine-type contract: arithmetic evidence is only meaningful if the core's
// finite integer domains match the language specification exactly.
// -----------------------------------------------------------------------------

CPPL_TEST(unsigned_1_bit_is_supported) {
    CPPL_CHECK(k::is_supported(kU1));
}

CPPL_TEST(signed_1_bit_is_supported) {
    CPPL_CHECK(k::is_supported(kI1));
}

CPPL_TEST(unsigned_2_bit_is_supported) {
    CPPL_CHECK(k::is_supported(kU2));
}

CPPL_TEST(signed_2_bit_is_supported) {
    CPPL_CHECK(k::is_supported(kI2));
}

CPPL_TEST(unsigned_8_bit_is_supported) {
    CPPL_CHECK(k::is_supported(kU8));
}

CPPL_TEST(signed_8_bit_is_supported) {
    CPPL_CHECK(k::is_supported(kI8));
}

CPPL_TEST(unsigned_16_bit_is_supported) {
    CPPL_CHECK(k::is_supported(kU16));
}

CPPL_TEST(signed_16_bit_is_supported) {
    CPPL_CHECK(k::is_supported(kI16));
}

CPPL_TEST(unsigned_32_bit_is_supported) {
    CPPL_CHECK(k::is_supported(kU32));
}

CPPL_TEST(signed_32_bit_is_supported) {
    CPPL_CHECK(k::is_supported(kI32));
}

CPPL_TEST(unsigned_63_bit_is_supported) {
    CPPL_CHECK(k::is_supported(kU63));
}

CPPL_TEST(signed_63_bit_is_supported) {
    CPPL_CHECK(k::is_supported(kI63));
}

CPPL_TEST(unsigned_64_bit_is_supported) {
    CPPL_CHECK(k::is_supported(kU64));
}

CPPL_TEST(signed_64_bit_is_supported) {
    CPPL_CHECK(k::is_supported(kI64));
}

CPPL_TEST(zero_width_integer_is_rejected) {
    CPPL_CHECK(!k::is_supported(k::IntType{0, k::Signedness::Unsigned}));
    CPPL_CHECK(!k::is_supported(k::IntType{0, k::Signedness::Signed}));
}

CPPL_TEST(width_above_64_is_rejected) {
    CPPL_CHECK(!k::is_supported(k::IntType{65, k::Signedness::Unsigned}));
    CPPL_CHECK(!k::is_supported(k::IntType{65, k::Signedness::Signed}));
}

CPPL_TEST(boolean_domain_is_exactly_unsigned_one_bit) {
    CPPL_CHECK_EQ(k::kBoolean, kU1);
    CPPL_CHECK_EQ(k::minimum_value(k::kBoolean), 0);
    CPPL_CHECK_EQ(k::maximum_value(k::kBoolean), 1);
}

CPPL_TEST(unsigned_8_bit_domain_has_exact_bounds) {
    CPPL_CHECK_EQ(k::minimum_value(kU8), 0);
    CPPL_CHECK_EQ(k::maximum_value(kU8), 255);
}

CPPL_TEST(signed_8_bit_domain_has_exact_bounds) {
    CPPL_CHECK_EQ(k::minimum_value(kI8), -128);
    CPPL_CHECK_EQ(k::maximum_value(kI8), 127);
}

CPPL_TEST(unsigned_32_bit_domain_has_exact_bounds) {
    CPPL_CHECK_EQ(k::minimum_value(kU32), 0);
    CPPL_CHECK_EQ(static_cast<k::Wide>(k::maximum_value(kU32)), (pow2(32) - 1));
}

CPPL_TEST(signed_32_bit_domain_has_exact_bounds) {
    CPPL_CHECK_EQ(k::minimum_value(kI32), std::numeric_limits<std::int32_t>::min());
    CPPL_CHECK_EQ(k::maximum_value(kI32), std::numeric_limits<std::int32_t>::max());
}

CPPL_TEST(signed_64_bit_domain_has_exact_bounds) {
    CPPL_CHECK_EQ(k::minimum_value(kI64), std::numeric_limits<std::int64_t>::min());
    CPPL_CHECK_EQ(k::maximum_value(kI64), std::numeric_limits<std::int64_t>::max());
}

CPPL_TEST(unsigned_64_bit_domain_exposes_its_true_maximum) {
    // SPEC conformance test. The current int64_t-returning API cannot represent
    // this value and is expected to fail until the API is widened/fixed.
    const k::Wide expected = pow2(64) - 1;
    CPPL_CHECK(static_cast<k::Wide>(k::maximum_value(kU64)) == expected);
}

CPPL_TEST(unsigned_64_bit_wrap_of_minus_one_is_true_maximum) {
    // Another deliberate API-pressure test: modulo 2^64 maps -1 to 2^64-1.
    const k::Wide expected = pow2(64) - 1;
    CPPL_CHECK(static_cast<k::Wide>(k::wrap_into(kU64, -1)) == expected);
}

CPPL_TEST(representability_is_exact_at_unsigned_8_bit_edges) {
    CPPL_CHECK(k::is_representable(kU8, 0));
    CPPL_CHECK(k::is_representable(kU8, 255));
    CPPL_CHECK(!k::is_representable(kU8, -1));
    CPPL_CHECK(!k::is_representable(kU8, 256));
}

CPPL_TEST(representability_is_exact_at_signed_8_bit_edges) {
    CPPL_CHECK(k::is_representable(kI8, -128));
    CPPL_CHECK(k::is_representable(kI8, 127));
    CPPL_CHECK(!k::is_representable(kI8, -129));
    CPPL_CHECK(!k::is_representable(kI8, 128));
}

CPPL_TEST(wrap_into_unsigned_8_bit_is_modulo_256) {
    CPPL_CHECK_EQ(k::wrap_into(kU8, -1), 255);
    CPPL_CHECK_EQ(k::wrap_into(kU8, 256), 0);
    CPPL_CHECK_EQ(k::wrap_into(kU8, 257), 1);
}

CPPL_TEST(wrap_into_signed_8_bit_is_twos_complement) {
    CPPL_CHECK_EQ(k::wrap_into(kI8, 128), -128);
    CPPL_CHECK_EQ(k::wrap_into(kI8, 255), -1);
    CPPL_CHECK_EQ(k::wrap_into(kI8, 256), 0);
}

// -----------------------------------------------------------------------------
// Primitive typing and normalization.
// -----------------------------------------------------------------------------

CPPL_TEST(addwrap_has_integer_result_type) {
    const k::Context context;
    const auto result = k::type_of(context, {}, add(kU8, lit(kU8, 1), lit(kU8, 2)));
    CPPL_CHECK(result.has_value());
    CPPL_CHECK_EQ(*result, type(kU8));
}

CPPL_TEST(comparison_has_boolean_result_type) {
    const k::Context context;
    const auto result = k::type_of(context, {}, compare(k::PrimOp::Less, kU8, lit(kU8, 1), lit(kU8, 2)));
    CPPL_CHECK(result.has_value());
    CPPL_CHECK_EQ(*result, type(k::kBoolean));
}

CPPL_TEST(addwrap_with_wrong_arity_is_rejected) {
    const k::Context context;
    const auto term = k::Term::primitive(k::PrimOp::AddWrap, kU8, {lit(kU8, 1)});
    const auto result = k::type_of(context, {}, term);
    CPPL_CHECK(!result.has_value());
    CPPL_CHECK_EQ(result.error().kind, k::CoreErrorKind::ArityMismatch);
}

CPPL_TEST(comparison_with_wrong_arity_is_rejected) {
    const k::Context context;
    const auto term = k::Term::primitive(k::PrimOp::Less, kU8, {lit(kU8, 1)});
    const auto result = k::type_of(context, {}, term);
    CPPL_CHECK(!result.has_value());
    CPPL_CHECK_EQ(result.error().kind, k::CoreErrorKind::ArityMismatch);
}

CPPL_TEST(addwrap_with_mixed_operand_types_is_rejected) {
    const k::Context context;
    const auto term = add(kU8, lit(kU8, 1), lit(kU16, 2));
    const auto result = k::type_of(context, {}, term);
    CPPL_CHECK(!result.has_value());
    CPPL_CHECK_EQ(result.error().kind, k::CoreErrorKind::TypeMismatch);
}

CPPL_TEST(out_of_scope_variable_is_rejected) {
    const k::Context context;
    const std::array<k::Type, 1> locals{type(kU32)};
    const auto result = k::type_of(context, locals, var(1));
    CPPL_CHECK(!result.has_value());
    CPPL_CHECK_EQ(result.error().kind, k::CoreErrorKind::VariableOutOfScope);
}

CPPL_TEST(unsigned_literal_outside_domain_is_rejected) {
    const k::Context context;
    const auto result = k::type_of(context, {}, lit(kU8, 256));
    CPPL_CHECK(!result.has_value());
    CPPL_CHECK_EQ(result.error().kind, k::CoreErrorKind::MalformedLiteral);
}

CPPL_TEST(signed_literal_outside_domain_is_rejected) {
    const k::Context context;
    const auto result = k::type_of(context, {}, lit(kI8, 128));
    CPPL_CHECK(!result.has_value());
    CPPL_CHECK_EQ(result.error().kind, k::CoreErrorKind::MalformedLiteral);
}

CPPL_TEST(unsigned_8_add_wraps_at_maximum) {
    const auto value = normalized_literal(add(kU8, lit(kU8, 255), lit(kU8, 1)));
    CPPL_CHECK(value.has_value());
    CPPL_CHECK_EQ(*value, 0);
}

CPPL_TEST(unsigned_8_sub_wraps_below_zero) {
    const auto value = normalized_literal(sub(kU8, lit(kU8, 0), lit(kU8, 1)));
    CPPL_CHECK(value.has_value());
    CPPL_CHECK_EQ(*value, 255);
}

CPPL_TEST(unsigned_8_mul_wraps) {
    const auto value = normalized_literal(mul(kU8, lit(kU8, 128), lit(kU8, 2)));
    CPPL_CHECK(value.has_value());
    CPPL_CHECK_EQ(*value, 0);
}

CPPL_TEST(signed_8_add_wraps_at_maximum) {
    const auto value = normalized_literal(add(kI8, lit(kI8, 127), lit(kI8, 1)));
    CPPL_CHECK(value.has_value());
    CPPL_CHECK_EQ(*value, -128);
}

CPPL_TEST(signed_8_sub_wraps_at_minimum) {
    const auto value = normalized_literal(sub(kI8, lit(kI8, -128), lit(kI8, 1)));
    CPPL_CHECK(value.has_value());
    CPPL_CHECK_EQ(*value, 127);
}

CPPL_TEST(signed_8_mul_wraps) {
    const auto value = normalized_literal(mul(kI8, lit(kI8, 64), lit(kI8, 2)));
    CPPL_CHECK(value.has_value());
    CPPL_CHECK_EQ(*value, -128);
}

CPPL_TEST(unsigned_16_add_wraps_at_maximum) {
    const auto value = normalized_literal(add(kU16, lit(kU16, 65535), lit(kU16, 1)));
    CPPL_CHECK(value.has_value());
    CPPL_CHECK_EQ(*value, 0);
}

CPPL_TEST(signed_16_add_wraps_at_maximum) {
    const auto value = normalized_literal(add(kI16, lit(kI16, 32767), lit(kI16, 1)));
    CPPL_CHECK(value.has_value());
    CPPL_CHECK_EQ(*value, -32768);
}

CPPL_TEST(unsigned_comparison_uses_unsigned_order) {
    const auto value = normalized_literal(compare(k::PrimOp::Less, kU8, lit(kU8, 255), lit(kU8, 1)));
    CPPL_CHECK(value.has_value());
    CPPL_CHECK_EQ(*value, 0);
}

CPPL_TEST(signed_comparison_uses_signed_order) {
    const auto value = normalized_literal(compare(k::PrimOp::Less, kI8, lit(kI8, -1), lit(kI8, 1)));
    CPPL_CHECK(value.has_value());
    CPPL_CHECK_EQ(*value, 1);
}

CPPL_TEST(select_with_true_literal_normalizes_to_true_branch) {
    const auto value = normalized_literal(select(kU8, lit(k::kBoolean, 1), lit(kU8, 7), lit(kU8, 9)));
    CPPL_CHECK(value.has_value());
    CPPL_CHECK_EQ(*value, 7);
}

CPPL_TEST(select_with_false_literal_normalizes_to_false_branch) {
    const auto value = normalized_literal(select(kU8, lit(k::kBoolean, 0), lit(kU8, 7), lit(kU8, 9)));
    CPPL_CHECK(value.has_value());
    CPPL_CHECK_EQ(*value, 9);
}

CPPL_TEST(not_true_normalizes_to_false) {
    const auto value = normalized_literal(unary(k::PrimOp::Not, k::kBoolean, lit(k::kBoolean, 1)));
    CPPL_CHECK(value.has_value());
    CPPL_CHECK_EQ(*value, 0);
}

CPPL_TEST(not_false_normalizes_to_true) {
    const auto value = normalized_literal(unary(k::PrimOp::Not, k::kBoolean, lit(k::kBoolean, 0)));
    CPPL_CHECK(value.has_value());
    CPPL_CHECK_EQ(*value, 1);
}

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
