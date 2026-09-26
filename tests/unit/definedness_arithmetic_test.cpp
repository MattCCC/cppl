// Linear arithmetic over representability, truncating division and integer
// conversion (RFC 0019), through the automation that proposes evidence and the
// kernel that checks it.
//
// Each value is pinned by order facts rather than an equality, so no rewriting
// can substitute it and fold the term away: whatever is proven is proven from
// the constraints the kernel states for the primitive. For every value of a
// small type the true statement must be proven and the false ones must not be.
// A false statement accepted would mean the kernel stated a constraint the
// machine does not satisfy, which is exactly the soundness property at stake.
// The expected values come from the host's own arithmetic.

#include "cppl/automation/evidence.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

#include <cstdint>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace k = cppl::kernel;
using Wide = k::Wide;

const k::IntType kI4{4, k::Signedness::Signed};
const k::IntType kU4{4, k::Signedness::Unsigned};
const k::IntType kI3{3, k::Signedness::Signed};
const k::IntType kU3{3, k::Signedness::Unsigned};
const k::IntType kI32{32, k::Signedness::Signed};
const k::IntType kU32{32, k::Signedness::Unsigned};

bool is_signed(const k::IntType& type) {
    return type.signedness == k::Signedness::Signed;
}

Wide least(const k::IntType& type) {
    return is_signed(type) ? -(Wide{1} << (type.width - 1)) : Wide{0};
}

Wide most(const k::IntType& type) {
    return is_signed(type) ? (Wide{1} << (type.width - 1)) - 1 : (Wide{1} << type.width) - 1;
}

// Two's-complement reduction by masking the bits, so no 128-bit division is
// needed: some targets have no runtime helper for one.
Wide reduce(const k::IntType& type, Wide value) {
    const k::WideUnsigned modulus = k::WideUnsigned{1} << type.width;
    const auto residue = static_cast<Wide>(static_cast<k::WideUnsigned>(value) & (modulus - 1u));
    return is_signed(type) && residue > most(type) ? residue - static_cast<Wide>(modulus) : residue;
}

// The host's truncating division, in 64 bits: every value here is small.
Wide host_quotient(Wide a, Wide b) {
    return static_cast<Wide>(static_cast<std::int64_t>(a) / static_cast<std::int64_t>(b));
}

Wide host_remainder(Wide a, Wide b) {
    return static_cast<Wide>(static_cast<std::int64_t>(a) % static_cast<std::int64_t>(b));
}

k::Term var(std::uint32_t index) {
    return k::Term::variable(k::VarIndex{index});
}

k::Term lit(const k::IntType& type, Wide value) {
    return k::Term::literal(type, value);
}

k::Term prim(k::PrimOp op, const k::IntType& type, std::vector<k::Term> arguments) {
    return k::Term::primitive(op, type, std::move(arguments));
}

k::Proposition truth(const k::Term& condition, bool holds) {
    return k::Proposition::equality(k::Type{k::kBoolean}, condition, lit(k::kBoolean, holds ? 1 : 0));
}

// value <= term <= value, as two order facts.
std::vector<k::Proposition> pinned(const k::IntType& type, const k::Term& term, Wide value) {
    return {truth(prim(k::PrimOp::LessEqual, type, {lit(type, value), term}), true),
            truth(prim(k::PrimOp::LessEqual, type, {term, lit(type, value)}), true)};
}

// forall binders. premises -> conclusion.
k::Proposition closed(const std::vector<k::Type>& binders, const std::vector<k::Proposition>& premises,
                      k::Proposition conclusion) {
    for (const auto& premise : std::views::reverse(premises)) {
        conclusion = k::Proposition::implication(premise, std::move(conclusion));
    }
    for (const auto& binder : std::views::reverse(binders)) {
        conclusion = k::Proposition::for_all(binder, std::move(conclusion));
    }
    return conclusion;
}

bool proven(const k::Proposition& goal) {
    const k::Context context;
    const auto evidence = cppl::automation::propose(context, goal);
    return evidence.has_value() && k::check(context, goal, evidence->proof, {}).has_value();
}

void expect(bool condition, const std::string& what) {
    if (!condition) {
        ::cppl::testing::fail(__FILE__, __LINE__, what);
    }
}

std::string at(const std::string& what, Wide a, Wide b) {
    return what + " at (" + k::describe(a) + ", " + k::describe(b) + ")";
}

} // namespace

// Over two unknowns of a 4-bit type, each pinned: add_fits and sub_fits are
// decided exactly, and so is mul_fits by a constant. A product of the two
// unknowns is not linear, so nothing decides it, and its falsity in particular
// must never be proven.
//
// SPEC: EQ-009, DEFINEDBEHAVIOR-001
CPPL_TEST(representability_is_decided_exactly_for_every_pinned_4_bit_pair) {
    for (const auto& type : {kI4, kU4}) {
        const std::vector<k::Type> binders{k::Type{type}, k::Type{type}};
        const auto x = var(1);
        const auto y = var(0);
        for (Wide a = least(type); a <= most(type); ++a) {
            for (Wide b = least(type); b <= most(type); ++b) {
                std::vector<k::Proposition> premises = pinned(type, x, a);
                for (auto& premise : pinned(type, y, b)) {
                    premises.push_back(std::move(premise));
                }
                const bool sum = a + b >= least(type) && a + b <= most(type);
                const bool difference = a - b >= least(type) && a - b <= most(type);
                const k::Term add = prim(k::PrimOp::AddFits, type, {x, y});
                const k::Term subtract = prim(k::PrimOp::SubFits, type, {x, y});
                expect(proven(closed(binders, premises, truth(add, sum))), at("add_fits", a, b));
                expect(!proven(closed(binders, premises, truth(add, !sum))), at("false add_fits", a, b));
                expect(proven(closed(binders, premises, truth(subtract, difference))), at("sub_fits", a, b));
                expect(!proven(closed(binders, premises, truth(subtract, !difference))), at("false sub_fits", a, b));
                // A product by a constant is linear, and decided; a product of
                // the two unknowns is not, and its falsity is never proven.
                const bool product = a * b >= least(type) && a * b <= most(type);
                const k::Term scaled = prim(k::PrimOp::MulFits, type, {x, lit(type, b)});
                expect(proven(closed(binders, premises, truth(scaled, product))), at("mul_fits", a, b));
                expect(!proven(closed(binders, premises, truth(scaled, !product))), at("false mul_fits", a, b));
                expect(!proven(closed(binders, premises, truth(prim(k::PrimOp::MulFits, type, {x, y}), !product))),
                       at("false mul_fits of unknowns", a, b));
            }
        }
    }
}

// A quotient and a remainder by a constant satisfy exactly truncating
// division: for every pinned dividend and every divisor but 0, 1 and -1
// (which fold), the host's quotient and remainder are proven and their
// neighbours are not.
//
// SPEC: ARITH-004, EQ-009
CPPL_TEST(division_by_a_constant_is_truncating_for_every_pinned_4_bit_dividend) {
    for (const auto& type : {kI4, kU4}) {
        const std::vector<k::Type> binders{k::Type{type}};
        const auto x = var(0);
        for (Wide a = least(type); a <= most(type); ++a) {
            for (Wide c = least(type); c <= most(type); ++c) {
                if (c >= -1 && c <= 1) {
                    continue;
                }
                const auto premises = pinned(type, x, a);
                // The host truncates toward zero; the least value over a
                // divisor of magnitude at least two cannot overflow.
                const Wide quotient = host_quotient(a, c);
                const Wide remainder = host_remainder(a, c);
                const k::Term q = prim(k::PrimOp::Quotient, type, {x, lit(type, c)});
                const k::Term r = prim(k::PrimOp::Remainder, type, {x, lit(type, c)});
                const auto equals = [&](const k::Term& term, Wide value) {
                    return closed(binders, premises, k::Proposition::equality(k::Type{type}, term, lit(type, value)));
                };
                expect(proven(equals(q, quotient)), at("quotient", a, c));
                expect(proven(equals(r, remainder)), at("remainder", a, c));
                for (const Wide wrong : {quotient - 1, quotient + 1}) {
                    if (wrong >= least(type) && wrong <= most(type)) {
                        expect(!proven(equals(q, wrong)), at("false quotient", a, c));
                    }
                }
                for (const Wide wrong : {remainder - 1, remainder + 1}) {
                    if (wrong >= least(type) && wrong <= most(type)) {
                        expect(!proven(equals(r, wrong)), at("false remainder", a, c));
                    }
                }
            }
        }
    }
}

// A remainder by an unknown divisor is bounded, never decided: no false value
// is proven for any pinned pair, a zero divisor included.
//
// SPEC: ARITH-004, DEFINEDBEHAVIOR-002
CPPL_TEST(a_remainder_by_an_unknown_divisor_proves_no_false_value) {
    for (const auto& type : {kI4, kU4}) {
        const std::vector<k::Type> binders{k::Type{type}, k::Type{type}};
        const auto x = var(1);
        const auto y = var(0);
        for (Wide a = least(type); a <= most(type); ++a) {
            for (Wide b = least(type); b <= most(type); ++b) {
                std::vector<k::Proposition> premises = pinned(type, x, a);
                for (auto& premise : pinned(type, y, b)) {
                    premises.push_back(std::move(premise));
                }
                const Wide remainder = b == 0 ? a : host_remainder(a, b);
                const k::Term r = prim(k::PrimOp::Remainder, type, {x, y});
                for (Wide wrong = least(type); wrong <= most(type); ++wrong) {
                    if (wrong != remainder) {
                        expect(!proven(closed(binders, premises,
                                              k::Proposition::equality(k::Type{type}, r, lit(type, wrong)))),
                               at("false remainder by an unknown divisor", a, b));
                    }
                }
            }
        }
    }
}

// What a remainder by an unknown divisor is known to satisfy, and what it is
// not: below a positive divisor, never above an unsigned dividend, and nothing
// about a zero divisor beyond that.
//
// SPEC: ARITH-004, EQ-009
CPPL_TEST(a_remainder_by_an_unknown_divisor_is_bounded_by_it) {
    const std::vector<k::Type> unsigned_pair{k::Type{kU32}, k::Type{kU32}};
    const std::vector<k::Type> signed_pair{k::Type{kI32}, k::Type{kI32}};
    const auto x = var(1);
    const auto y = var(0);
    const auto below = [](const k::IntType& type, k::Term a, k::Term b) {
        return truth(prim(k::PrimOp::Less, type, {std::move(a), std::move(b)}), true);
    };
    const auto r_u = prim(k::PrimOp::Remainder, kU32, {x, y});
    const auto r_i = prim(k::PrimOp::Remainder, kI32, {x, y});
    CPPL_CHECK(proven(closed(unsigned_pair, {below(kU32, lit(kU32, 0), y)}, below(kU32, r_u, y))));
    CPPL_CHECK(!proven(closed(unsigned_pair, {}, below(kU32, r_u, y))));
    CPPL_CHECK(proven(closed(unsigned_pair, {}, truth(prim(k::PrimOp::LessEqual, kU32, {r_u, x}), true))));
    CPPL_CHECK(proven(closed(signed_pair, {below(kI32, lit(kI32, 0), y)}, below(kI32, r_i, y))));
    // A negative dividend leaves a remainder that is not negative only if zero.
    CPPL_CHECK(proven(closed(signed_pair, {below(kI32, x, lit(kI32, 0))},
                             truth(prim(k::PrimOp::LessEqual, kI32, {r_i, lit(kI32, 0)}), true))));
    CPPL_CHECK(!proven(closed(signed_pair, {below(kI32, x, lit(kI32, 0))},
                              truth(prim(k::PrimOp::Less, kI32, {r_i, lit(kI32, 0)}), true))));
}

// A conversion equals its operand where the target holds every source value,
// and otherwise is the operand reduced modulo 2^width: for every pinned value,
// the host's reduction is proven and its neighbours are not.
//
// SPEC: ARITH-002, EQ-011
CPPL_TEST(conversion_is_reduction_for_every_pinned_value_between_small_types) {
    const std::vector<std::pair<k::IntType, k::IntType>> conversions{
        {kU4, kI3}, {kI4, kU3}, {kI4, kI3}, {kU4, kU3}, {kI3, kU4}, {kU3, kI4}, {kI4, kU4}, {kU4, kI4}, {kI3, kI4}};
    for (const auto& [from, to] : conversions) {
        const std::vector<k::Type> binders{k::Type{from}};
        const auto x = var(0);
        const k::Term converted = prim(k::PrimOp::Convert, to, {x});
        for (Wide a = least(from); a <= most(from); ++a) {
            const auto premises = pinned(from, x, a);
            const Wide expected = reduce(to, a);
            const auto equals = [&](Wide value) {
                return closed(binders, premises, k::Proposition::equality(k::Type{to}, converted, lit(to, value)));
            };
            expect(proven(equals(expected)), at("conversion", a, expected));
            for (Wide wrong = least(to); wrong <= most(to); ++wrong) {
                if (wrong != expected) {
                    expect(!proven(equals(wrong)), at("false conversion", a, wrong));
                }
            }
        }
    }
}

// The obligation a signed operation owes, as its lowering states it: proven
// one step inside the type's boundary, refused at the boundary, for every
// operation the language models.
//
// SPEC: EQ-009, DEFINEDBEHAVIOR-001
CPPL_TEST(signed_obligations_hold_one_step_inside_the_boundary_and_fail_at_it) {
    const std::vector<k::Type> one{k::Type{kI32}};
    const auto x = var(0);
    const auto fact = [&](k::PrimOp op, Wide bound) {
        return truth(prim(op, kI32, {x, lit(kI32, bound)}), true);
    };
    const auto fits = [&](k::PrimOp op, Wide operand) {
        return truth(prim(op, kI32, {x, lit(kI32, operand)}), true);
    };
    // x + 1 is defined below the maximum, and not at it.
    CPPL_CHECK(proven(closed(one, {fact(k::PrimOp::Less, 2147483647)}, fits(k::PrimOp::AddFits, 1))));
    CPPL_CHECK(!proven(closed(one, {fact(k::PrimOp::LessEqual, 2147483647)}, fits(k::PrimOp::AddFits, 1))));
    // x - 1 is defined above the minimum, and not at it.
    CPPL_CHECK(proven(closed(one, {fact(k::PrimOp::Greater, -2147483648)}, fits(k::PrimOp::SubFits, 1))));
    CPPL_CHECK(!proven(closed(one, {fact(k::PrimOp::GreaterEqual, -2147483648)}, fits(k::PrimOp::SubFits, 1))));
    // x * 1000 is defined for |x| <= 2147483 and not for x = 2147484.
    CPPL_CHECK(proven(closed(one, {fact(k::PrimOp::LessEqual, 2147483), fact(k::PrimOp::GreaterEqual, -2147483)},
                             fits(k::PrimOp::MulFits, 1000))));
    CPPL_CHECK(!proven(closed(one, {fact(k::PrimOp::LessEqual, 2147484), fact(k::PrimOp::GreaterEqual, -2147483)},
                              fits(k::PrimOp::MulFits, 1000))));
    // -x is defined above the minimum.
    const auto negation = truth(prim(k::PrimOp::SubFits, kI32, {lit(kI32, 0), x}), true);
    CPPL_CHECK(proven(closed(one, {fact(k::PrimOp::Greater, -2147483648)}, negation)));
    CPPL_CHECK(!proven(closed(one, {}, negation)));
    // Once x + 1 is known to fit, it is greater than x; without that it is not.
    const auto successor =
        truth(prim(k::PrimOp::Greater, kI32, {prim(k::PrimOp::AddWrap, kI32, {x, lit(kI32, 1)}), x}), true);
    CPPL_CHECK(proven(closed(one, {fits(k::PrimOp::AddFits, 1)}, successor)));
    CPPL_CHECK(!proven(closed(one, {}, successor)));
}
