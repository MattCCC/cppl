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
#include "cppl/testing/parallel.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
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

// ---- what the kernel states of the new primitives holds of the machine ----
//
// A certificate refutes a system only when no integer assignment satisfies it,
// so a constraint the kernel states about a primitive that some machine
// assignment violates would let a false goal be proven, whatever the
// certificate. The folding tests above never reach those constraints: they are
// stated only where an operand is not a literal.
//
// Each system below is built from facts true of one assignment of its binders,
// with the goal False, which adds nothing. The assignment is then extended to
// every variable the kernel introduced -- a value by evaluating its term with
// the evaluator above, a multiple of 2^width as the constraints that mention it
// alone pin it -- and every constraint, and one member of every disjunction,
// must hold. Then no certificate can refute the system: a Farkas sum of
// satisfied constraints is satisfied, and a split or a case always has a
// satisfied side. Every pair of 8-bit values is enumerated; wider types are
// sampled from a fixed seed, with their edges.

namespace {

// C++'s wrapping ring operation, computed in 128 unsigned bits, which wraps
// modulo 2^128 and so agrees with every narrower width.
Wide ring(const k::IntType& type, k::PrimOp op, Wide a, Wide b) {
    const auto x = static_cast<k::WideUnsigned>(a);
    const auto y = static_cast<k::WideUnsigned>(b);
    const k::WideUnsigned result = op == k::PrimOp::AddWrap ? x + y : op == k::PrimOp::SubWrap ? x - y : x * y;
    return reduce(type, static_cast<Wide>(result));
}

// The machine value of a term whose binders hold `binders`, outermost first,
// computed with the evaluator above and nothing of the kernel's.
std::optional<Wide> evaluate(const k::Term& term, const std::vector<Wide>& binders) {
    if (const auto* variable = std::get_if<k::Var>(&term.node)) {
        const std::size_t index = variable->index.value;
        if (index >= binders.size()) {
            return std::nullopt;
        }
        return binders[binders.size() - 1 - index];
    }
    if (const auto* literal = std::get_if<k::Literal>(&term.node)) {
        return reduce(literal->type, literal->value);
    }
    const auto* primitive = std::get_if<k::Prim>(&term.node);
    if (primitive == nullptr) {
        return std::nullopt;
    }
    std::vector<Wide> operands;
    for (const k::Term& argument : primitive->arguments) {
        const std::optional<Wide> operand = evaluate(argument, binders);
        if (!operand.has_value()) {
            return std::nullopt;
        }
        operands.push_back(*operand);
    }
    const k::IntType& type = primitive->type;
    const auto truth_of = [](bool holds) {
        return Wide{holds ? 1 : 0};
    };
    if (operands.size() != k::arity(primitive->op)) {
        return std::nullopt;
    }
    switch (primitive->op) {
        case k::PrimOp::AddWrap:
        case k::PrimOp::SubWrap:
        case k::PrimOp::MulWrap:
            return ring(type, primitive->op, operands[0], operands[1]);
        case k::PrimOp::Equal:
            return truth_of(operands[0] == operands[1]);
        case k::PrimOp::NotEqual:
            return truth_of(operands[0] != operands[1]);
        case k::PrimOp::Less:
            return truth_of(operands[0] < operands[1]);
        case k::PrimOp::LessEqual:
            return truth_of(operands[0] <= operands[1]);
        case k::PrimOp::Greater:
            return truth_of(operands[0] > operands[1]);
        case k::PrimOp::GreaterEqual:
            return truth_of(operands[0] >= operands[1]);
        case k::PrimOp::Not:
            return truth_of(operands[0] == 0);
        case k::PrimOp::Select:
            return operands[0] != 0 ? operands[1] : operands[2];
        case k::PrimOp::AddFits:
        case k::PrimOp::SubFits:
        case k::PrimOp::MulFits:
            return truth_of(fits(type, primitive->op, operands[0], operands[1]));
        case k::PrimOp::Quotient:
            return reduce(type, divided(operands[0], operands[1]).first);
        case k::PrimOp::Remainder:
            return divided(operands[0], operands[1]).second;
        case k::PrimOp::Convert:
            return reduce(type, operands[0]);
    }
    return std::nullopt;
}

// sum(coefficient * value) + constant, where every variable of the constraint
// but `skip` has a value; none when the sum leaves 128 bits.
std::optional<Wide> left_side(const k::LinearConstraint& constraint, const std::vector<std::optional<Wide>>& values,
                              std::optional<std::uint32_t> skip = std::nullopt) {
    Wide sum = constraint.constant;
    for (const auto& [variable, coefficient] : constraint.terms) {
        if (skip.has_value() && variable == *skip) {
            continue;
        }
        if (variable >= values.size()) {
            return std::nullopt;
        }
        const std::optional<Wide>& held = values[variable];
        if (!held.has_value()) {
            return std::nullopt;
        }
        Wide product = 0;
        if (__builtin_mul_overflow(coefficient, *held, &product) || __builtin_add_overflow(sum, product, &sum)) {
            return std::nullopt;
        }
    }
    return sum;
}

Wide floor_quotient(Wide numerator, Wide denominator) {
    Wide quotient = numerator / denominator;
    if (numerator % denominator != 0 && (numerator < 0) != (denominator < 0)) {
        --quotient;
    }
    return quotient;
}

// Why the machine assignment extended to the system's variables does not
// satisfy it, or nothing when it does.
std::optional<std::string> unsatisfied(const k::ArithmeticSystem& system, const std::vector<Wide>& binders) {
    std::vector<std::optional<Wide>> values(system.variables.size());
    for (std::size_t index = 0; index < system.variables.size(); ++index) {
        const k::ArithmeticVariable& variable = system.variables[index];
        if (variable.role != k::VariableRole::Value) {
            continue;
        }
        const std::optional<Wide> evaluated = evaluate(variable.term, binders);
        if (!evaluated.has_value()) {
            return "the term of variable " + std::to_string(index) + ", " + k::describe(variable.term) +
                   ", cannot be evaluated";
        }
        values[index] = evaluated;
        if (*evaluated < least(variable.type) || *evaluated > most(variable.type)) {
            return "variable " + std::to_string(index) + ", " + k::describe(variable.term) +
                   ", evaluates outside its type";
        }
    }
    // A multiple of 2^width is pinned by the constraints in which every other
    // variable already has a value; earlier multiples are solved first, since a
    // later one's constraints may mention them.
    for (std::uint32_t index = 0; index < system.variables.size(); ++index) {
        if (system.variables[index].role != k::VariableRole::Wrap) {
            continue;
        }
        std::optional<Wide> lowest;
        std::optional<Wide> highest;
        for (const k::LinearConstraint& constraint : system.constraints) {
            const auto own = std::ranges::find(constraint.terms, index, &std::pair<std::uint32_t, Wide>::first);
            if (own == constraint.terms.end()) {
                continue;
            }
            const std::optional<Wide> rest = left_side(constraint, values, index);
            if (!rest.has_value()) {
                continue;
            }
            // coefficient * w + rest <= 0
            const Wide coefficient = own->second;
            if (coefficient > 0) {
                const Wide bound = floor_quotient(-*rest, coefficient);
                highest = highest.has_value() ? std::min(*highest, bound) : bound;
            } else {
                const Wide bound = -floor_quotient(-*rest, -coefficient);
                lowest = lowest.has_value() ? std::max(*lowest, bound) : bound;
            }
        }
        const std::string term = k::describe(system.variables[index].term);
        if (!lowest.has_value() || !highest.has_value()) {
            return "the multiple of 2^width for " + term + " is not pinned";
        }
        if (*lowest > *highest) {
            return "no multiple of 2^width satisfies what is stated of " + term;
        }
        values[index] = lowest;
    }
    // A constraint as it stands under the assignment, for a failure message.
    const auto shown = [&](const k::LinearConstraint& constraint) {
        std::string text;
        for (const auto& [variable, coefficient] : constraint.terms) {
            text += k::describe(coefficient) + " * [" + k::describe(system.variables[variable].term) + " = " +
                    (values[variable].has_value() ? k::describe(*values[variable]) : std::string("?")) + "] + ";
        }
        return text + k::describe(constraint.constant) + " <= 0";
    };
    for (std::size_t index = 0; index < system.constraints.size(); ++index) {
        const std::optional<Wide> sum = left_side(system.constraints[index], values);
        if (!sum.has_value() || *sum > 0) {
            return "constraint " + std::to_string(index) +
                   " is violated by the machine assignment: " + shown(system.constraints[index]);
        }
    }
    for (std::size_t index = 0; index < system.disjunctions.size(); ++index) {
        const bool holds = std::ranges::any_of(system.disjunctions[index], [&](const k::LinearConstraint& member) {
            const std::optional<Wide> sum = left_side(member, values);
            return sum.has_value() && *sum <= 0;
        });
        if (!holds) {
            return "neither member of disjunction " + std::to_string(index) + " holds of the machine assignment";
        }
    }
    return std::nullopt;
}

k::Proposition equals(const k::IntType& type, k::Term lhs, k::Term rhs) {
    return k::Proposition::equality(k::Type{type}, std::move(lhs), std::move(rhs));
}

// Facts about x and y of `type`, each true where x is `a` and y is `b`: every
// primitive of RFC 0019 over unknown operands, over a constant operand, over a
// wrapped sum, and as a value.
std::vector<k::Proposition> binary_facts(const k::IntType& type, Wide a, Wide b) {
    const k::Term x = var(1);
    const k::Term y = var(0);
    std::vector<k::Proposition> facts{equals(type, x, lit(type, a)), equals(type, y, lit(type, b))};
    for (const k::PrimOp op : {k::PrimOp::AddFits, k::PrimOp::SubFits, k::PrimOp::MulFits}) {
        facts.push_back(truth(prim(op, type, {x, y}), fits(type, op, a, b)));
        facts.push_back(truth(prim(op, type, {y, x}), fits(type, op, b, a)));
        // By a constant: linear, so stated as bounds on the exact result. The
        // one product this test's own 128-bit sums cannot evaluate, of two
        // 64-bit values near their greatest, is left out.
        if (exact(op, a, b).has_value()) {
            facts.push_back(truth(prim(op, type, {x, lit(type, b)}), fits(type, op, a, b)));
        }
    }
    // A wrapped operand: its own multiple of 2^width stands inside the bound.
    const Wide next = reduce(type, a + 1);
    const k::Term bumped = prim(k::PrimOp::AddWrap, type, {x, lit(type, 1)});
    facts.push_back(truth(prim(k::PrimOp::AddFits, type, {bumped, y}), fits(type, k::PrimOp::AddFits, next, b)));
    facts.push_back(truth(prim(k::PrimOp::SubFits, type, {y, bumped}), fits(type, k::PrimOp::SubFits, b, next)));
    // Representability as a value, not a condition.
    const Wide chosen = fits(type, k::PrimOp::AddFits, a, b) ? ring(type, k::PrimOp::AddWrap, a, b) : 0;
    facts.push_back(
        equals(type,
               prim(k::PrimOp::Select, type,
                    {prim(k::PrimOp::AddFits, type, {x, y}), prim(k::PrimOp::AddWrap, type, {x, y}), lit(type, 0)}),
               lit(type, chosen)));
    // Quotient and remainder by an unknown divisor, by a constant one, and of a
    // wrapped dividend.
    const auto [quotient, remainder] = divided(a, b);
    facts.push_back(equals(type, prim(k::PrimOp::Quotient, type, {x, y}), lit(type, reduce(type, quotient))));
    facts.push_back(equals(type, prim(k::PrimOp::Remainder, type, {x, y}), lit(type, remainder)));
    facts.push_back(
        equals(type, prim(k::PrimOp::Quotient, type, {x, lit(type, b)}), lit(type, reduce(type, quotient))));
    facts.push_back(equals(type, prim(k::PrimOp::Remainder, type, {x, lit(type, b)}), lit(type, remainder)));
    const Wide sum = ring(type, k::PrimOp::AddWrap, a, b);
    const k::Term summed = prim(k::PrimOp::AddWrap, type, {x, y});
    facts.push_back(equals(type, prim(k::PrimOp::Remainder, type, {summed, y}), lit(type, divided(sum, b).second)));
    facts.push_back(equals(type, prim(k::PrimOp::Quotient, type, {summed, lit(type, b)}),
                           lit(type, reduce(type, divided(sum, b).first))));
    // A product of two widened values, decided from the ranges they came from
    // where every such product fits, and an unknown boolean where one may not.
    for (const k::IntType& wider : {kI16, kU16, kI32, kI64}) {
        if (wider.width <= type.width) {
            continue;
        }
        const k::Term product = prim(k::PrimOp::MulFits, wider,
                                     {prim(k::PrimOp::Convert, wider, {x}), prim(k::PrimOp::Convert, wider, {y})});
        facts.push_back(truth(product, fits(wider, k::PrimOp::MulFits, reduce(wider, a), reduce(wider, b))));
    }
    return facts;
}

// Facts about a conversion of x, of `from`, into every type.
std::vector<k::Proposition> conversion_facts(const k::IntType& from, Wide a) {
    const k::Term x = var(0);
    std::vector<k::Proposition> facts{equals(from, x, lit(from, a))};
    for (const k::IntType& to : kWide) {
        facts.push_back(equals(to, prim(k::PrimOp::Convert, to, {x}), lit(to, reduce(to, a))));
        // A conversion of a wrapped value.
        const k::Term bumped = prim(k::PrimOp::AddWrap, from, {x, lit(from, 1)});
        facts.push_back(equals(to, prim(k::PrimOp::Convert, to, {bumped}), lit(to, reduce(to, reduce(from, a + 1)))));
    }
    return facts;
}

void satisfied(const std::vector<k::Proposition>& facts, const std::vector<k::Type>& locals,
               const std::vector<Wide>& binders, const std::string& context) {
    const auto system = k::arithmetic_system({}, facts, k::Proposition::falsity(), {}, locals);
    if (!system) {
        ::cppl::testing::fail(__FILE__, __LINE__, context + ": the kernel stated no system: " + system.error().detail);
    }
    if (const std::optional<std::string> problem = unsatisfied(*system, binders)) {
        ::cppl::testing::fail(__FILE__, __LINE__, context + ": " + *problem);
    }
}

void binary_case(const k::IntType& type, Wide a, Wide b, std::uint64_t seed) {
    satisfied(binary_facts(type, a, b), {k::Type{type}, k::Type{type}}, {a, b},
              "facts of " + k::describe(type) + " x = " + k::describe(a) + ", y = " + k::describe(b) + " seed " +
                  std::to_string(seed));
}

} // namespace

// Every pair of both 8-bit types, numbered in the order a loop over the type,
// then x, then y takes them. The pairs share nothing, so they are checked on
// several threads at once (cppl/testing/parallel.hpp); a failure still names
// the first pair that order reaches.
//
// SPEC: EQ-009, EQ-010, ARITH-006, ARITH-007, DEFINEDBEHAVIOR-001, DEFINEDBEHAVIOR-002, DEFINEDBEHAVIOR-003
CPPL_TEST(every_machine_assignment_satisfies_what_the_kernel_states_of_8_bit_operations) {
    const std::array<k::IntType, 2> kTypes{kI8, kU8};
    constexpr std::size_t kValues = 256; // of an 8-bit type
    ::cppl::testing::for_each_index(kTypes.size() * kValues * kValues, [&](std::size_t index) {
        const k::IntType& type = kTypes.at(index / (kValues * kValues));
        const Wide a = least(type) + static_cast<Wide>(index / kValues % kValues);
        const Wide b = least(type) + static_cast<Wide>(index % kValues);
        binary_case(type, a, b, 0);
    });
}

// SPEC: EQ-009, EQ-010, ARITH-006, ARITH-007, DEFINEDBEHAVIOR-001
CPPL_TEST(every_machine_assignment_satisfies_what_the_kernel_states_of_sampled_wider_operations) {
    Random random{kSeed ^ 0x5157u};
    for (const auto& type : kWide) {
        if (type.width == 8) {
            continue;
        }
        const std::vector<Wide> at = edges(type);
        for (const Wide a : at) {
            for (const Wide b : at) {
                binary_case(type, a, b, kSeed ^ 0x5157u);
            }
        }
        for (int index = 0; index < 300; ++index) {
            const Wide a = reduce(type, static_cast<Wide>(random.next()));
            const std::uint64_t drawn = random.next();
            const Wide b = index % 2 == 0 ? reduce(type, static_cast<Wide>(drawn))
                                          : reduce(type, static_cast<Wide>(drawn % 33u) - 16);
            binary_case(type, a, b, kSeed ^ 0x5157u);
        }
    }
}

// SPEC: ARITH-008, EQ-003, EQ-009
CPPL_TEST(every_machine_assignment_satisfies_what_the_kernel_states_of_conversions) {
    Random random{kSeed ^ 0xc1u};
    for (const auto& from : kWide) {
        std::vector<Wide> values = edges(from);
        if (from.width == 8) {
            for (Wide value = least(from); value <= most(from); ++value) {
                values.push_back(value);
            }
        }
        for (int index = 0; index < 100; ++index) {
            values.push_back(reduce(from, static_cast<Wide>(random.next())));
        }
        for (const Wide value : values) {
            satisfied(conversion_facts(from, value), {k::Type{from}}, {value},
                      "conversions of " + k::describe(from) + " " + k::describe(value) + " seed " +
                          std::to_string(kSeed ^ 0xc1u));
        }
    }
}

// The check above is not vacuous: a fact false of the assignment it extends is
// reported, whether it states a wrong remainder or a representability that
// fails.
//
// SPEC: EQ-010
CPPL_TEST(a_false_fact_is_found_unsatisfied_by_the_machine_assignment) {
    const std::vector<k::Type> locals{k::Type{kI8}, k::Type{kI8}};
    const std::vector<Wide> binders{-7, 2};
    const auto build = [&](const k::Proposition& fact) {
        auto facts =
            std::vector<k::Proposition>{equals(kI8, var(1), lit(kI8, -7)), equals(kI8, var(0), lit(kI8, 2)), fact};
        return k::arithmetic_system({}, facts, k::Proposition::falsity(), {}, locals).value();
    };
    CPPL_CHECK(
        !unsatisfied(build(equals(kI8, prim(k::PrimOp::Remainder, kI8, {var(1), var(0)}), lit(kI8, -1))), binders)
             .has_value());
    CPPL_CHECK(unsatisfied(build(equals(kI8, prim(k::PrimOp::Remainder, kI8, {var(1), var(0)}), lit(kI8, 1))), binders)
                   .has_value());
    CPPL_CHECK(unsatisfied(build(truth(prim(k::PrimOp::AddFits, kI8, {var(1), lit(kI8, -128)}))), binders).has_value());
}
