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

#include "arithmetic_test_support.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace arithmetic_test_detail;

namespace {

const k::IntType kI2{2, k::Signedness::Signed};

const k::IntType kI16{16, k::Signedness::Signed};

const k::IntType kI63{63, k::Signedness::Signed};

k::Term unary(k::PrimOp op, const k::IntType& integer, k::Term a) {
    return k::Term::primitive(op, integer, {std::move(a)});
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
