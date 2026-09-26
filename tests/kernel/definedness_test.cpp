// The primitives RFC 0019 adds to the kernel: whether an exact sum, difference
// or product is representable, truncating quotient and remainder, and integer
// conversion (SPEC.md EQ-002, EQ-003, ARITH-004).
//
// Each is checked against an evaluator written here from the host's own
// arithmetic -- native 64-bit division and the compiler's checked 128-bit
// builtins -- which shares no code with the kernel. Every pair of 8-bit values
// is enumerated. Wider types are sampled from a fixed seed, which every failure
// reports, together with the edges of each type.

#include "cppl/kernel/box.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/linear.hpp"
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
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace k = cppl::kernel;
using Wide = k::Wide;

const k::IntType kI8{8, k::Signedness::Signed};
const k::IntType kU8{8, k::Signedness::Unsigned};
const k::IntType kI16{16, k::Signedness::Signed};
const k::IntType kU16{16, k::Signedness::Unsigned};
const k::IntType kI32{32, k::Signedness::Signed};
const k::IntType kU32{32, k::Signedness::Unsigned};
const k::IntType kI64{64, k::Signedness::Signed};
const k::IntType kU64{64, k::Signedness::Unsigned};

constexpr std::uint64_t kSeed = 0x5eed0019c0ffee11u;

// ---- the independent evaluator ----

bool is_signed(const k::IntType& type) {
    return type.signedness == k::Signedness::Signed;
}

Wide least(const k::IntType& type) {
    return is_signed(type) ? -(Wide{1} << (type.width - 1)) : Wide{0};
}

Wide most(const k::IntType& type) {
    return is_signed(type) ? (Wide{1} << (type.width - 1)) - 1 : (Wide{1} << type.width) - 1;
}

// Two's-complement reduction: the value's residue modulo 2^width, read back
// with the type's signedness.
Wide reduce(const k::IntType& type, Wide value) {
    const k::WideUnsigned modulus = k::WideUnsigned{1} << type.width;
    const k::WideUnsigned residue = static_cast<k::WideUnsigned>(value) & (modulus - 1u);
    if (is_signed(type) && residue >= modulus / 2u) {
        return static_cast<Wide>(residue) - static_cast<Wide>(modulus);
    }
    return static_cast<Wide>(residue);
}

// The integer result of the operation, if it exists in 128 bits.
std::optional<Wide> exact(k::PrimOp op, Wide a, Wide b) {
    Wide result = 0;
    bool overflowed = true;
    if (op == k::PrimOp::AddFits) {
        overflowed = __builtin_add_overflow(a, b, &result);
    } else if (op == k::PrimOp::SubFits) {
        overflowed = __builtin_sub_overflow(a, b, &result);
    } else {
        overflowed = __builtin_mul_overflow(a, b, &result);
    }
    return overflowed ? std::nullopt : std::optional<Wide>{result};
}

bool fits(const k::IntType& type, k::PrimOp op, Wide a, Wide b) {
    const auto result = exact(op, a, b);
    return result.has_value() && *result >= least(type) && *result <= most(type);
}

// C++'s truncating division on the host, made total the way the kernel's is:
// a zero divisor gives a zero quotient and leaves the dividend as remainder.
// Every value is within 64 bits, so native 64-bit division is used; the one
// quotient it cannot hold, the least int64 over -1, is 2^63.
std::pair<Wide, Wide> divided(Wide a, Wide b) {
    if (b == 0) {
        return {0, a};
    }
    if (a >= 0 && b > 0) {
        const auto x = static_cast<std::uint64_t>(a);
        const auto y = static_cast<std::uint64_t>(b);
        return {static_cast<Wide>(x / y), static_cast<Wide>(x % y)};
    }
    const auto x = static_cast<std::int64_t>(a);
    const auto y = static_cast<std::int64_t>(b);
    if (x == std::numeric_limits<std::int64_t>::min() && y == -1) {
        return {Wide{1} << 63, 0};
    }
    return {static_cast<Wide>(x / y), static_cast<Wide>(x % y)};
}

// ---- the kernel ----

k::Term lit(const k::IntType& type, Wide value) {
    return k::Term::literal(type, value);
}

k::Term var(std::uint32_t index) {
    return k::Term::variable(k::VarIndex{index});
}

k::Term prim(k::PrimOp op, const k::IntType& type, std::vector<k::Term> arguments) {
    return k::Term::primitive(op, type, std::move(arguments));
}

// The literal a closed term normalizes to, failing the test if it does not.
Wide folded(const k::Term& term, const std::string& context) {
    const auto normal = k::normalize({}, term, {});
    if (!normal) {
        ::cppl::testing::fail(__FILE__, __LINE__, context + ": normalization failed: " + normal.error().detail);
    }
    const auto* literal = std::get_if<k::Literal>(&normal->node);
    if (literal == nullptr) {
        ::cppl::testing::fail(__FILE__, __LINE__, context + ": did not fold to a literal: " + k::describe(*normal));
    }
    return literal->value;
}

std::string named(const k::IntType& type, k::PrimOp op, Wide a, Wide b, std::uint64_t seed) {
    return k::describe(op) + ":" + k::describe(type) + "(" + k::describe(a) + ", " + k::describe(b) + ") seed " +
           std::to_string(seed);
}

// The kernel's folding of every binary primitive of RFC 0019 on one pair,
// against the evaluator. Where the sum, difference or product fits, the ring
// operation the lowering states it with must equal it: that is what lets a
// signed operation be stated with it without modeling overflow as wrapping
// (EQ-002).
void agree_on(const k::IntType& type, Wide a, Wide b, std::uint64_t seed) {
    static constexpr std::array<std::pair<k::PrimOp, k::PrimOp>, 3> kFits{
        std::pair{k::PrimOp::AddFits, k::PrimOp::AddWrap}, std::pair{k::PrimOp::SubFits, k::PrimOp::SubWrap},
        std::pair{k::PrimOp::MulFits, k::PrimOp::MulWrap}};
    for (const auto& [op, ring] : kFits) {
        const std::string context = named(type, op, a, b, seed);
        const bool expected = fits(type, op, a, b);
        const Wide decided = folded(prim(op, type, {lit(type, a), lit(type, b)}), context);
        if (decided != (expected ? 1 : 0)) {
            ::cppl::testing::fail(__FILE__, __LINE__, context + ": representability disagrees");
        }
        const Wide computed = folded(prim(ring, type, {lit(type, a), lit(type, b)}), context);
        const std::optional<Wide> result = exact(op, a, b);
        if (expected && result && computed != *result) {
            ::cppl::testing::fail(__FILE__, __LINE__, context + ": a representable result is not the ring's");
        }
        if (computed != reduce(type, result.value_or(reduce(type, computed)))) {
            ::cppl::testing::fail(__FILE__, __LINE__, context + ": the ring operation is not reduction");
        }
    }
    const auto [quotient, remainder] = divided(a, b);
    const std::string context = named(type, k::PrimOp::Quotient, a, b, seed);
    if (folded(prim(k::PrimOp::Quotient, type, {lit(type, a), lit(type, b)}), context) != reduce(type, quotient)) {
        ::cppl::testing::fail(__FILE__, __LINE__, context + ": quotient disagrees");
    }
    if (folded(prim(k::PrimOp::Remainder, type, {lit(type, a), lit(type, b)}), context) != remainder) {
        ::cppl::testing::fail(__FILE__, __LINE__, context + ": remainder disagrees");
    }
}

struct Random {
    std::uint64_t state;
    std::uint64_t next() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
};

std::vector<Wide> edges(const k::IntType& type) {
    std::vector<Wide> values{least(type), least(type) + 1, most(type), most(type) - 1, 0, 1, 2, 3, 7};
    if (is_signed(type)) {
        values.insert(values.end(), {-1, -2, -3, -7});
    }
    return values;
}

const std::array<k::IntType, 8> kWide{kI8, kU8, kI16, kU16, kI32, kU32, kI64, kU64};

} // namespace

// SPEC: ARITH-006, ARITH-007, EQ-002, DEFINEDBEHAVIOR-001, DEFINEDBEHAVIOR-003
CPPL_TEST(folding_agrees_with_the_host_on_every_pair_of_8_bit_values) {
    for (const auto& type : {kI8, kU8}) {
        for (Wide a = least(type); a <= most(type); ++a) {
            for (Wide b = least(type); b <= most(type); ++b) {
                agree_on(type, a, b, 0);
            }
        }
    }
}

// SPEC: ARITH-006, ARITH-007, EQ-002
CPPL_TEST(folding_agrees_with_the_host_at_the_edges_and_on_sampled_wider_values) {
    Random random{kSeed};
    for (const auto& type : kWide) {
        const std::vector<Wide> at = edges(type);
        for (const Wide a : at) {
            for (const Wide b : at) {
                agree_on(type, a, b, kSeed);
            }
        }
        for (int index = 0; index < 4000; ++index) {
            const Wide a = reduce(type, static_cast<Wide>(random.next()));
            // Small divisors and factors are where the interesting quotients
            // and near-overflows are, so half the samples draw one.
            const std::uint64_t drawn = random.next();
            const Wide b = index % 2 == 0 ? reduce(type, static_cast<Wide>(drawn))
                                          : reduce(type, static_cast<Wide>(drawn % 33u) - 16);
            agree_on(type, a, b, kSeed);
        }
    }
}

// SPEC: ARITH-008, EQ-003
CPPL_TEST(conversions_reduce_every_8_bit_value_and_sampled_wider_ones_into_every_type) {
    Random random{kSeed ^ 0xc0u};
    for (const auto& from : kWide) {
        std::vector<Wide> values = edges(from);
        if (from.width == 8) {
            for (Wide value = least(from); value <= most(from); ++value) {
                values.push_back(value);
            }
        }
        for (int index = 0; index < 500; ++index) {
            values.push_back(reduce(from, static_cast<Wide>(random.next())));
        }
        for (const auto& to : kWide) {
            for (const Wide value : values) {
                const std::string context = "convert " + k::describe(from) + " " + k::describe(value) + " to " +
                                            k::describe(to) + " seed " + std::to_string(kSeed ^ 0xc0u);
                if (folded(prim(k::PrimOp::Convert, to, {lit(from, value)}), context) != reduce(to, value)) {
                    ::cppl::testing::fail(__FILE__, __LINE__, context + ": conversion disagrees");
                }
            }
        }
    }
}

// SPEC: ARITH-007, DEFINEDBEHAVIOR-002, DEFINEDBEHAVIOR-003
CPPL_TEST(division_folds_only_where_the_total_definition_decides_it) {
    const auto x = var(0);
    const auto normal = [](const k::Term& term) {
        auto result = k::normalize({}, term, {});
        CPPL_CHECK(result.has_value());
        return *result;
    };
    // A zero divisor: the quotient is 0 and the remainder is the dividend.
    CPPL_CHECK(normal(prim(k::PrimOp::Quotient, kI32, {x, lit(kI32, 0)})) == lit(kI32, 0));
    CPPL_CHECK(normal(prim(k::PrimOp::Remainder, kI32, {x, lit(kI32, 0)})) == x);
    // By one and by minus one; the least value over -1 wraps to itself.
    CPPL_CHECK(normal(prim(k::PrimOp::Quotient, kI32, {x, lit(kI32, 1)})) == x);
    CPPL_CHECK(normal(prim(k::PrimOp::Remainder, kU32, {x, lit(kU32, 1)})) == lit(kU32, 0));
    CPPL_CHECK(normal(prim(k::PrimOp::Quotient, kI32, {x, lit(kI32, -1)})) ==
               normal(prim(k::PrimOp::SubWrap, kI32, {lit(kI32, 0), x})));
    CPPL_CHECK(normal(prim(k::PrimOp::Quotient, kI32, {lit(kI32, -2147483648), lit(kI32, -1)})) ==
               lit(kI32, -2147483648));
    CPPL_CHECK(normal(prim(k::PrimOp::Remainder, kI32, {lit(kI32, -2147483648), lit(kI32, -1)})) == lit(kI32, 0));
    // Truncation toward zero, and the remainder has the dividend's sign.
    CPPL_CHECK(normal(prim(k::PrimOp::Quotient, kI32, {lit(kI32, -7), lit(kI32, 2)})) == lit(kI32, -3));
    CPPL_CHECK(normal(prim(k::PrimOp::Remainder, kI32, {lit(kI32, -7), lit(kI32, 2)})) == lit(kI32, -1));
    CPPL_CHECK(normal(prim(k::PrimOp::Quotient, kI32, {lit(kI32, 7), lit(kI32, -2)})) == lit(kI32, -3));
    CPPL_CHECK(normal(prim(k::PrimOp::Remainder, kI32, {lit(kI32, 7), lit(kI32, -2)})) == lit(kI32, 1));
    // An unsigned divisor of all ones is a large divisor, not -1.
    CPPL_CHECK(normal(prim(k::PrimOp::Quotient, kU8, {lit(kU8, 254), lit(kU8, 255)})) == lit(kU8, 0));
    CPPL_CHECK(!(normal(prim(k::PrimOp::Quotient, kU8, {x, lit(kU8, 255)})) ==
                 normal(prim(k::PrimOp::SubWrap, kU8, {lit(kU8, 0), x}))));
    // Anything else is one opaque term, never rewritten as another division.
    const auto halved = prim(k::PrimOp::Quotient, kI32, {x, lit(kI32, 2)});
    CPPL_CHECK(normal(halved) == halved);
    CPPL_CHECK(!(normal(prim(k::PrimOp::MulWrap, kI32, {halved, lit(kI32, 2)})) == x));
}

// SPEC: EQ-003
CPPL_TEST(representability_is_commutative_where_the_operation_is) {
    const auto x = var(1);
    const auto y = var(0);
    const auto normal = [](const k::Term& term) {
        auto result = k::normalize({}, term, {});
        CPPL_CHECK(result.has_value());
        return *result;
    };
    CPPL_CHECK(normal(prim(k::PrimOp::AddFits, kI32, {x, y})) == normal(prim(k::PrimOp::AddFits, kI32, {y, x})));
    CPPL_CHECK(normal(prim(k::PrimOp::MulFits, kI32, {x, y})) == normal(prim(k::PrimOp::MulFits, kI32, {y, x})));
    // A difference is not symmetric: x - MIN overflows where MIN - x need not.
    CPPL_CHECK(!(normal(prim(k::PrimOp::SubFits, kI32, {x, y})) == normal(prim(k::PrimOp::SubFits, kI32, {y, x}))));
}

// SPEC: EQ-005, EQ-009
CPPL_TEST(the_primitives_are_typed) {
    const std::vector<k::Type> binders{k::Type{kI32}, k::Type{kU8}};
    const auto i = var(1);
    const auto u = var(0);
    const auto type_of = [&](const k::Term& term) {
        return k::type_of({}, binders, term, {});
    };
    CPPL_CHECK(type_of(prim(k::PrimOp::AddFits, kI32, {i, i})).value() == k::Type{k::kBoolean});
    CPPL_CHECK(type_of(prim(k::PrimOp::Quotient, kI32, {i, i})).value() == k::Type{kI32});
    CPPL_CHECK(type_of(prim(k::PrimOp::Convert, kI32, {u})).value() == k::Type{kI32});
    // A conversion takes one integer of any type; the others one pair of the
    // stated type.
    CPPL_CHECK(!type_of(prim(k::PrimOp::AddFits, kI32, {i, u})).has_value());
    CPPL_CHECK(!type_of(prim(k::PrimOp::Remainder, kU8, {i, u})).has_value());
    CPPL_CHECK(!type_of(prim(k::PrimOp::MulFits, kI32, {i})).has_value());
    CPPL_CHECK(!type_of(prim(k::PrimOp::Quotient, kI32, {i, i, i})).has_value());
    CPPL_CHECK(!type_of(prim(k::PrimOp::Convert, kI32, {u, u})).has_value());
    CPPL_CHECK(!type_of(prim(k::PrimOp::Convert, kI32, {})).has_value());
    CPPL_CHECK(!type_of(prim(k::PrimOp::Convert, kI32, {var(2)})).has_value());
    const k::Type value = k::Type::value("opaque");
    CPPL_CHECK(!k::type_of({}, std::vector<k::Type>{value}, prim(k::PrimOp::Convert, kI32, {var(0)}), {}).has_value());
    CPPL_CHECK(!type_of(prim(k::PrimOp::Convert, k::IntType{65, k::Signedness::Signed}, {u})).has_value());
    // An operation code outside the enumeration is no primitive at all.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    CPPL_CHECK(!type_of(prim(static_cast<k::PrimOp>(200), kI32, {i, i})).has_value());
}

// ---- linear arithmetic over the new primitives ----

namespace {

k::Proposition truth(const k::Term& condition, bool holds = true) {
    return k::Proposition::equality(k::Type{k::kBoolean}, condition, lit(k::kBoolean, holds ? 1 : 0));
}

k::Proposition compared(k::PrimOp op, const k::IntType& type, k::Term a, k::Term b) {
    return truth(prim(op, type, {std::move(a), std::move(b)}));
}

} // namespace

// SPEC: EQ-009, DEFINEDBEHAVIOR-001
CPPL_TEST(the_kernel_states_representability_as_bounds_on_the_unbounded_result) {
    const auto x = var(0);
    const std::vector<k::Type> locals{k::Type{kI32}};
    // x + 1 fits: one value variable, and -2^31 <= x + 1 <= 2^31 - 1 beside
    // the type's own bounds on x.
    const auto holds =
        k::arithmetic_system({}, {}, truth(prim(k::PrimOp::AddFits, kI32, {x, lit(kI32, 1)}), false), {}, locals);
    CPPL_CHECK(holds.has_value());
    CPPL_CHECK_EQ(holds->variables.size(), std::size_t{1});
    CPPL_CHECK_EQ(holds->constraints.size(), std::size_t{4});
    CPPL_CHECK(holds->disjunctions.empty());
    // The negation of a goal that it fits is that it lies outside: a
    // disjunction of below the least value and above the greatest.
    const auto negated =
        k::arithmetic_system({}, {}, truth(prim(k::PrimOp::AddFits, kI32, {x, lit(kI32, 1)})), {}, locals);
    CPPL_CHECK(negated.has_value());
    CPPL_CHECK_EQ(negated->disjunctions.size(), std::size_t{1});
    // A product of two unknowns is not linear, so its representability is an
    // unknown boolean and bounds nothing.
    const auto product =
        k::arithmetic_system({}, {}, truth(prim(k::PrimOp::MulFits, kI32, {x, x})), {}, std::vector<k::Type>{});
    CPPL_CHECK(product.has_value());
    CPPL_CHECK(product->disjunctions.empty());
}

// SPEC: ARITH-008, EQ-009, EQ-011
CPPL_TEST(a_conversion_is_stated_only_where_its_operand_type_is_known) {
    const auto x = var(0);
    const auto goal = k::Proposition::equality(k::Type{kI32}, prim(k::PrimOp::Convert, kI32, {x}), lit(kI32, 0));
    // With no binder types the operand's type is unknown, and the step is
    // refused rather than guessed.
    CPPL_CHECK(!k::arithmetic_system({}, {}, goal, {}).has_value());
    // A widening conversion equals its operand; a narrowing one differs from it
    // by a multiple of 2^width.
    const auto widening = k::arithmetic_system({}, {}, goal, {}, std::vector<k::Type>{k::Type{kU8}});
    CPPL_CHECK(widening.has_value());
    CPPL_CHECK_EQ(widening->variables.size(), std::size_t{2});
    const auto narrowing = k::arithmetic_system({}, {}, goal, {}, std::vector<k::Type>{k::Type{kI64}});
    CPPL_CHECK(narrowing.has_value());
    CPPL_CHECK_EQ(narrowing->variables.size(), std::size_t{3});
    CPPL_CHECK(narrowing->variables[2].role == k::VariableRole::Wrap);
}

// Soundness of the certificate rule over the new facts: a certificate for a
// true goal is not one for a false goal. Each is built by hand, so this checks
// the kernel alone.
//
// SPEC: EQ-010, DEFINEDBEHAVIOR-001
CPPL_TEST(a_hand_built_representability_certificate_proves_only_what_holds) {
    // forall x : i32. x <= 2147483646 -> add_fits(x, 1).
    const auto x = var(0);
    const auto premise = compared(k::PrimOp::LessEqual, kI32, x, lit(kI32, 2147483646));
    const auto check = [&](const k::Proposition& premise_stated, k::ArithmeticCertificate certificate) {
        const auto goal = k::Proposition::for_all(
            k::Type{kI32},
            k::Proposition::implication(premise_stated, truth(prim(k::PrimOp::AddFits, kI32, {x, lit(kI32, 1)}))));
        std::vector<k::ArithmeticFact> facts;
        facts.push_back(
            k::ArithmeticFact{premise_stated, k::Box<k::ProofTerm>{k::ProofTerm::hypothesis(k::HypothesisIndex{0})}});
        const auto proof = k::ProofTerm::forall_introduction(
            k::Type{kI32},
            k::ProofTerm::implication_introduction(
                premise_stated, k::ProofTerm::linear_arithmetic(std::move(facts), std::move(certificate))));
        return k::check({}, goal, proof, {}).has_value();
    };
    // Constraints: 0: -x - 2^31 <= 0, 1: x - (2^31 - 1) <= 0, 2: x - 2147483646 <= 0; the goal's negation is the
    // disjunction 0: [x + 1 <= -2^31 - 1] | [2^31 - (x + 1) <= 0].
    const auto farkas = [](std::vector<std::pair<std::uint32_t, Wide>> multipliers) {
        return k::ArithmeticCertificate{k::FarkasSum{std::move(multipliers)}};
    };
    const auto cases =
        k::ArithmeticCertificate{k::DisjunctionCases{0, k::Box<k::ArithmeticCertificate>{farkas({{0, 1}, {3, 1}})},
                                                     k::Box<k::ArithmeticCertificate>{farkas({{2, 1}, {3, 1}})}}};
    CPPL_CHECK(check(premise, cases));
    // The same certificate does not prove it from a premise one step weaker:
    // x <= 2147483647 is every value, and add_fits(2147483647, 1) is false.
    CPPL_CHECK(!check(compared(k::PrimOp::LessEqual, kI32, x, lit(kI32, 2147483647)), cases));
    // Nor does a certificate that refutes one side of the disjunction only.
    const auto one_sided =
        k::ArithmeticCertificate{k::DisjunctionCases{0, k::Box<k::ArithmeticCertificate>{farkas({{0, 1}, {3, 1}})},
                                                     k::Box<k::ArithmeticCertificate>{farkas({{0, 1}, {3, 1}})}}};
    CPPL_CHECK(!check(premise, one_sided));
}
