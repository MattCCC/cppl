// Machine arithmetic in the kernel: the polynomial normal form behind
// definitional equality, canonical comparisons, and the linear-arithmetic rule.
//
// Normalization is checked against an evaluator written here, independently of
// the kernel: for random terms over small types, a term and its normal form
// must agree on every assignment. That is the property definitional equality
// relies on, tested exhaustively rather than by example.

#include "cppl/kernel/box.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/linear.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace k = cppl::kernel;

const k::IntType kU32{32, k::Signedness::Unsigned};
const k::IntType kI32{32, k::Signedness::Signed};
const k::IntType kU8{8, k::Signedness::Unsigned};
const k::IntType kI8{8, k::Signedness::Signed};

k::Term var(std::uint32_t index) {
    return k::Term::variable(k::VarIndex{index});
}
k::Term lit(const k::IntType& type, k::Wide value) {
    return k::Term::literal(type, value);
}
k::Term prim(k::PrimOp op, const k::IntType& type, std::vector<k::Term> arguments) {
    return k::Term::primitive(op, type, std::move(arguments));
}
k::Term add(const k::IntType& t, k::Term a, k::Term b) {
    return prim(k::PrimOp::AddWrap, t, {std::move(a), std::move(b)});
}
k::Term sub(const k::IntType& t, k::Term a, k::Term b) {
    return prim(k::PrimOp::SubWrap, t, {std::move(a), std::move(b)});
}
k::Term mul(const k::IntType& t, k::Term a, k::Term b) {
    return prim(k::PrimOp::MulWrap, t, {std::move(a), std::move(b)});
}

k::Term normal(const k::Term& term) {
    auto result = k::normalize({}, term, {});
    CPPL_CHECK(result.has_value());
    return *result;
}

// Whether reflexivity proves `lhs = rhs` at `type`, closed over `binders`
// variables of that type.
bool definitional(const k::IntType& type, const k::Term& lhs, const k::Term& rhs, std::uint32_t binders = 2,
                  const k::Type* result = nullptr) {
    const k::Type stated = result != nullptr ? *result : k::Type{type};
    k::Proposition goal = k::Proposition::equality(stated, lhs, rhs);
    k::ProofTerm proof = k::ProofTerm::reflexivity();
    for (std::uint32_t index = 0; index < binders; ++index) {
        goal = k::Proposition::for_all(k::Type{type}, std::move(goal));
        proof = k::ProofTerm::forall_introduction(k::Type{type}, std::move(proof));
    }
    return k::check({}, goal, proof, {}).has_value();
}

// ---- an evaluator independent of the kernel ----

std::uint64_t mask(const k::IntType& type) {
    return type.width >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << type.width) - 1u;
}

std::int64_t as_signed(const k::IntType& type, std::uint64_t bits) {
    if (type.width >= 64)
        return static_cast<std::int64_t>(bits);
    const std::uint64_t sign = std::uint64_t{1} << (type.width - 1);
    return (bits & sign) != 0 ? static_cast<std::int64_t>(bits) - static_cast<std::int64_t>(mask(type)) - 1
                              : static_cast<std::int64_t>(bits);
}

bool before(const k::IntType& type, std::uint64_t a, std::uint64_t b) {
    return type.signedness == k::Signedness::Signed ? as_signed(type, a) < as_signed(type, b) : a < b;
}

std::uint64_t evaluate(const k::Term& term, const std::vector<std::uint64_t>& environment) {
    if (const auto* variable = std::get_if<k::Var>(&term.node)) {
        return environment[environment.size() - 1 - variable->index.value];
    }
    if (const auto* literal = std::get_if<k::Literal>(&term.node)) {
        return static_cast<std::uint64_t>(literal->value) & mask(literal->type);
    }
    const auto& node = std::get<k::Prim>(term.node);
    std::vector<std::uint64_t> values;
    values.reserve(node.arguments.size());
    for (const auto& argument : node.arguments)
        values.push_back(evaluate(argument, environment));
    const auto& t = node.type;
    switch (node.op) {
        case k::PrimOp::AddWrap:
            return (values[0] + values[1]) & mask(t);
        case k::PrimOp::SubWrap:
            return (values[0] - values[1]) & mask(t);
        case k::PrimOp::MulWrap:
            return (values[0] * values[1]) & mask(t);
        case k::PrimOp::Equal:
            return values[0] == values[1] ? 1u : 0u;
        case k::PrimOp::NotEqual:
            return values[0] != values[1] ? 1u : 0u;
        case k::PrimOp::Less:
            return before(t, values[0], values[1]) ? 1u : 0u;
        case k::PrimOp::LessEqual:
            return !before(t, values[1], values[0]) ? 1u : 0u;
        case k::PrimOp::Greater:
            return before(t, values[1], values[0]) ? 1u : 0u;
        case k::PrimOp::GreaterEqual:
            return !before(t, values[0], values[1]) ? 1u : 0u;
        case k::PrimOp::Not:
            return values[0] == 0 ? 1u : 0u;
        case k::PrimOp::Select:
            return values[0] != 0 ? values[1] : values[2];
    }
    return 0;
}

struct Random {
    std::uint64_t state;
    std::uint64_t next() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
    std::uint64_t below(std::uint64_t bound) {
        return next() % bound;
    }
};

k::Term random_term(Random& random, const k::IntType& type, std::uint32_t variables, int depth);

k::Term random_condition(Random& random, const k::IntType& type, std::uint32_t variables, int depth) {
    static const k::PrimOp comparisons[] = {k::PrimOp::Equal,     k::PrimOp::NotEqual, k::PrimOp::Less,
                                            k::PrimOp::LessEqual, k::PrimOp::Greater,  k::PrimOp::GreaterEqual};
    k::Term condition =
        prim(comparisons[random.below(6)], type,
             {random_term(random, type, variables, depth), random_term(random, type, variables, depth)});
    if (random.below(4) == 0)
        condition = prim(k::PrimOp::Not, k::kBoolean, {condition});
    return condition;
}

k::Term random_term(Random& random, const k::IntType& type, std::uint32_t variables, int depth) {
    const std::uint64_t choice = depth <= 0 ? random.below(2) : random.below(9);
    switch (choice) {
        case 0:
            return var(static_cast<std::uint32_t>(random.below(variables)));
        case 1:
            return lit(type, k::wrap_into(type, static_cast<k::Wide>(random.next())));
        case 2:
            return add(type, random_term(random, type, variables, depth - 1),
                       random_term(random, type, variables, depth - 1));
        case 3:
            return sub(type, random_term(random, type, variables, depth - 1),
                       random_term(random, type, variables, depth - 1));
        case 4:
            return mul(type, random_term(random, type, variables, depth - 1),
                       random_term(random, type, variables, depth - 1));
        case 5:
            return prim(k::PrimOp::Select, type,
                        {random_condition(random, type, variables, depth - 1),
                         random_term(random, type, variables, depth - 1),
                         random_term(random, type, variables, depth - 1)});
        case 6:
            return mul(type, lit(type, k::wrap_into(type, static_cast<k::Wide>(random.below(7)) - 3)),
                       random_term(random, type, variables, depth - 1));
        case 7:
            return add(type, random_term(random, type, variables, depth - 1), lit(type, 1));
        default:
            return sub(type, random_term(random, type, variables, depth - 1),
                       var(static_cast<std::uint32_t>(random.below(variables))));
    }
}

// Every assignment of `variables` variables of a small type.
void each_assignment(const k::IntType& type, std::uint32_t variables,
                     const std::function<void(const std::vector<std::uint64_t>&)>& visit) {
    // A 64-bit type has 2^64 values, one more than a std::uint64_t counts, so
    // the count would wrap to zero; only a type this small can be enumerated.
    if (type.width > 16) {
        ::cppl::testing::fail(__FILE__, __LINE__, "each_assignment enumerates types of at most 16 bits");
    }
    std::vector<std::uint64_t> environment(variables, 0);
    const std::uint64_t size = mask(type) + 1u;
    std::uint64_t total = 1;
    for (std::uint32_t index = 0; index < variables; ++index)
        total *= size;
    for (std::uint64_t code = 0; code < total; ++code) {
        std::uint64_t rest = code;
        for (auto& value : environment) {
            value = rest % size;
            rest /= size;
        }
        visit(environment);
    }
}

void check_normal_forms_agree(const k::IntType& type, std::uint64_t seed, int terms) {
    Random random{seed};
    for (int index = 0; index < terms; ++index) {
        const k::Term term = index % 3 == 0 ? random_condition(random, type, 2, 3) : random_term(random, type, 2, 4);
        const auto normalized = k::normalize({}, term, {});
        CPPL_CHECK(normalized.has_value());
        // Normal forms are fixed points.
        CPPL_CHECK(normal(*normalized) == *normalized);
        each_assignment(type, 2, [&](const std::vector<std::uint64_t>& environment) {
            CPPL_CHECK_EQ(evaluate(*normalized, environment), evaluate(term, environment));
        });
    }
}

} // namespace

CPPL_TEST(normal_forms_agree_with_the_machine_on_every_assignment) {
    check_normal_forms_agree(k::IntType{4, k::Signedness::Unsigned}, 0x9e3779b97f4a7c15u, 400);
    check_normal_forms_agree(k::IntType{4, k::Signedness::Signed}, 0x243f6a8885a308d3u, 400);
    check_normal_forms_agree(k::IntType{3, k::Signedness::Unsigned}, 0x13198a2e03707344u, 300);
    check_normal_forms_agree(k::IntType{1, k::Signedness::Unsigned}, 0xa4093822299f31d0u, 200);
}

CPPL_TEST(normal_forms_agree_at_64_bits_at_the_edges_of_the_type) {
    for (const auto& type : {k::IntType{64, k::Signedness::Unsigned}, k::IntType{64, k::Signedness::Signed}}) {
        Random random{0x082efa98ec4e6c89u};
        const std::uint64_t edges[] = {
            0u, 1u, ~std::uint64_t{0}, std::uint64_t{1} << 63, (std::uint64_t{1} << 63) - 1u, 0x5555555555555555u};
        for (int index = 0; index < 300; ++index) {
            const k::Term term = random_term(random, type, 2, 4);
            const k::Term normalized = normal(term);
            CPPL_CHECK(normal(normalized) == normalized);
            for (const auto first : edges) {
                for (const auto second : edges) {
                    const std::vector<std::uint64_t> environment{first, second};
                    CPPL_CHECK_EQ(evaluate(normalized, environment), evaluate(term, environment));
                }
            }
        }
    }
}

CPPL_TEST(ring_identities_hold_definitionally) {
    const auto x = var(1);
    const auto y = var(0);
    const auto one = lit(kU32, 1);
    CPPL_CHECK(definitional(kU32, add(kU32, x, y), add(kU32, y, x)));
    CPPL_CHECK(definitional(kU32, add(kU32, add(kU32, x, one), one), add(kU32, x, lit(kU32, 2))));
    CPPL_CHECK(definitional(kU32, mul(kU32, x, add(kU32, y, one)), add(kU32, mul(kU32, y, x), x)));
    CPPL_CHECK(definitional(kU32, sub(kU32, x, x), lit(kU32, 0)));
    CPPL_CHECK(definitional(kU32, sub(kU32, add(kU32, x, y), y), x));
    CPPL_CHECK(definitional(kU32, mul(kU32, lit(kU32, 2), x), add(kU32, x, x)));
    CPPL_CHECK(definitional(kU32, mul(kU32, x, lit(kU32, 0)), lit(kU32, 0)));
    // Wrapping folds at the width: 0 - 1 is the largest u8.
    CPPL_CHECK(definitional(kU8, sub(kU8, lit(kU8, 0), lit(kU8, 1)), lit(kU8, 255), 0));
    CPPL_CHECK(definitional(kU8, mul(kU8, lit(kU8, 16), lit(kU8, 16)), lit(kU8, 0), 0));
    // The same ring at a signed type: the kernel primitive wraps; it is the
    // lowering's business never to use it for signed C++ arithmetic.
    CPPL_CHECK(definitional(kI8, add(kI8, lit(kI8, 127), lit(kI8, 1)), lit(kI8, -128), 0));
}

CPPL_TEST(non_identities_are_not_definitional) {
    const auto x = var(1);
    const auto y = var(0);
    CPPL_CHECK(!definitional(kU32, add(kU32, x, lit(kU32, 1)), x));
    CPPL_CHECK(!definitional(kU32, sub(kU32, x, y), sub(kU32, y, x)));
    CPPL_CHECK(!definitional(kU32, mul(kU32, x, x), x));
    CPPL_CHECK(!definitional(kU32, mul(kU32, x, y), add(kU32, x, y)));
    CPPL_CHECK(!definitional(kU8, add(kU8, lit(kU8, 200), lit(kU8, 100)), lit(kU8, 300 - 256 + 1), 0));
}

CPPL_TEST(comparisons_are_decided_only_where_the_machine_type_decides_them) {
    const auto x = var(1);
    const auto y = var(0);
    const k::Type boolean{k::kBoolean};
    const auto holds = [&](const k::IntType& type, k::PrimOp op, k::Term a, k::Term b) {
        return definitional(type, prim(op, type, {std::move(a), std::move(b)}), lit(k::kBoolean, 1), 2, &boolean);
    };
    const auto fails = [&](const k::IntType& type, k::PrimOp op, k::Term a, k::Term b) {
        return definitional(type, prim(op, type, {std::move(a), std::move(b)}), lit(k::kBoolean, 0), 2, &boolean);
    };
    // Unsigned values are at least zero and at most the maximum.
    CPPL_CHECK(holds(kU32, k::PrimOp::GreaterEqual, x, lit(kU32, 0)));
    CPPL_CHECK(fails(kU32, k::PrimOp::Less, x, lit(kU32, 0)));
    CPPL_CHECK(holds(kU32, k::PrimOp::LessEqual, x, lit(kU32, 4294967295)));
    // Signed values are not.
    CPPL_CHECK(!holds(kI32, k::PrimOp::GreaterEqual, x, lit(kI32, 0)));
    CPPL_CHECK(fails(kI32, k::PrimOp::Less, x, lit(kI32, -2147483648)));
    // Irreflexive and reflexive orders.
    CPPL_CHECK(fails(kU32, k::PrimOp::Less, x, x));
    CPPL_CHECK(holds(kI32, k::PrimOp::LessEqual, x, x));
    CPPL_CHECK(holds(kU32, k::PrimOp::Equal, add(kU32, x, y), add(kU32, y, x)));
    // Equality cancels in the ring; order does not, because it wraps.
    CPPL_CHECK(definitional(kU32,
                            prim(k::PrimOp::Equal, kU32, {add(kU32, x, lit(kU32, 1)), add(kU32, y, lit(kU32, 1))}),
                            prim(k::PrimOp::Equal, kU32, {x, y}), 2, &boolean));
    CPPL_CHECK(!definitional(kU32,
                             prim(k::PrimOp::Less, kU32, {add(kU32, x, lit(kU32, 1)), add(kU32, y, lit(kU32, 1))}),
                             prim(k::PrimOp::Less, kU32, {x, y}), 2, &boolean));
    CPPL_CHECK(!holds(kU32, k::PrimOp::Greater, add(kU32, x, lit(kU32, 1)), x));
    // Swapped and negated forms are one comparison.
    CPPL_CHECK(
        definitional(kU32, prim(k::PrimOp::Greater, kU32, {x, y}), prim(k::PrimOp::Less, kU32, {y, x}), 2, &boolean));
    CPPL_CHECK(definitional(kU32, prim(k::PrimOp::GreaterEqual, kU32, {x, y}),
                            prim(k::PrimOp::Not, k::kBoolean, {prim(k::PrimOp::Less, kU32, {x, y})}), 2, &boolean));
}

CPPL_TEST(selection_with_equal_arms_or_a_negated_condition_is_canonical) {
    const auto x = var(1);
    const auto y = var(0);
    const auto condition = prim(k::PrimOp::Less, kU32, {x, y});
    CPPL_CHECK(definitional(kU32, prim(k::PrimOp::Select, kU32, {condition, x, x}), x));
    CPPL_CHECK(definitional(kU32, prim(k::PrimOp::Select, kU32, {prim(k::PrimOp::Not, k::kBoolean, {condition}), x, y}),
                            prim(k::PrimOp::Select, kU32, {condition, y, x})));
    CPPL_CHECK(!definitional(kU32, prim(k::PrimOp::Select, kU32, {condition, x, y}), x));
}

CPPL_TEST(a_normalization_budget_rejects_an_exploding_product) {
    // (x + y)^12 has 13 monomials; (x0 + ... + x7)^6 has far more than the
    // limit allows, and is refused rather than approximated.
    k::Term sum = var(0);
    for (std::uint32_t index = 1; index < 8; ++index)
        sum = add(kU32, sum, var(index));
    k::Term power = sum;
    for (int index = 1; index < 6; ++index)
        power = mul(kU32, power, sum);
    const auto result = k::normalize({}, power, {});
    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == k::CoreErrorKind::NormalizationBudgetExhausted);
}

// ---- linear arithmetic ----

namespace {

k::Proposition comparison(k::PrimOp op, const k::IntType& type, k::Term a, k::Term b, bool holds = true) {
    return k::Proposition::equality(k::Type{k::kBoolean}, prim(op, type, {std::move(a), std::move(b)}),
                                    lit(k::kBoolean, holds ? 1 : 0));
}

// forall x : u32. x <= 10 -> x < 20, closed by the given certificate.
k::CheckResult bounded(k::ArithmeticCertificate certificate, bool true_goal = true) {
    const auto premise = comparison(k::PrimOp::LessEqual, kU32, var(0), lit(kU32, 10));
    const auto conclusion = comparison(k::PrimOp::Less, kU32, var(0), lit(kU32, true_goal ? 20 : 5));
    const auto goal = k::Proposition::for_all(k::Type{kU32}, k::Proposition::implication(premise, conclusion));
    std::vector<k::ArithmeticFact> facts;
    facts.push_back(k::ArithmeticFact{premise, k::Box<k::ProofTerm>{k::ProofTerm::hypothesis(k::HypothesisIndex{0})}});
    const auto proof = k::ProofTerm::forall_introduction(
        k::Type{kU32}, k::ProofTerm::implication_introduction(
                           premise, k::ProofTerm::linear_arithmetic(std::move(facts), std::move(certificate))));
    return k::check({}, goal, proof, {});
}

k::ArithmeticCertificate farkas(std::vector<std::pair<std::uint32_t, k::Wide>> multipliers) {
    return k::ArithmeticCertificate{k::FarkasSum{std::move(multipliers)}};
}

} // namespace

CPPL_TEST(the_kernel_states_the_arithmetic_system_itself) {
    const auto x = var(0);
    const std::vector<k::Proposition> facts{comparison(k::PrimOp::LessEqual, kU32, x, lit(kU32, 10))};
    const auto system = k::arithmetic_system({}, facts, comparison(k::PrimOp::Less, kU32, x, lit(kU32, 20)), {});
    CPPL_CHECK(system.has_value());
    // x bounded below and above by u32, then x - 10 <= 0, then 20 - x <= 0.
    CPPL_CHECK_EQ(system->variables.size(), std::size_t{1});
    CPPL_CHECK_EQ(system->constraints.size(), std::size_t{4});
    CPPL_CHECK(system->constraints[2].constant == -10);
    CPPL_CHECK(system->constraints[3].constant == 20);
    CPPL_CHECK(system->disjunctions.empty());
}

CPPL_TEST(a_farkas_sum_proves_an_order_consequence) {
    CPPL_CHECK(bounded(farkas({{2, 1}, {3, 1}})).has_value());
    // Scaling a valid combination keeps it valid.
    CPPL_CHECK(bounded(farkas({{2, 3}, {3, 3}})).has_value());
}

CPPL_TEST(malformed_or_insufficient_certificates_are_refused) {
    // Not a contradiction: one constraint alone, the wrong constraints, a
    // variable left over, a nonpositive or repeated multiplier, an index out
    // of range.
    CPPL_CHECK(!bounded(farkas({})).has_value());
    CPPL_CHECK(!bounded(farkas({{2, 1}})).has_value());
    CPPL_CHECK(!bounded(farkas({{0, 1}, {1, 1}})).has_value());
    CPPL_CHECK(!bounded(farkas({{2, 1}, {3, 2}})).has_value());
    CPPL_CHECK(!bounded(farkas({{2, 0}, {3, 1}})).has_value());
    CPPL_CHECK(!bounded(farkas({{2, -1}, {3, -1}})).has_value());
    CPPL_CHECK(!bounded(farkas({{3, 1}, {2, 1}})).has_value());
    CPPL_CHECK(!bounded(farkas({{2, 1}, {2, 1}, {3, 1}})).has_value());
    CPPL_CHECK(!bounded(farkas({{2, 1}, {9, 1}})).has_value());
    // A disjunction that does not exist, and a split naming no variable.
    CPPL_CHECK(!bounded(k::ArithmeticCertificate{
                            k::DisjunctionCases{0, k::Box<k::ArithmeticCertificate>{farkas({{2, 1}, {3, 1}})},
                                                k::Box<k::ArithmeticCertificate>{farkas({{2, 1}, {3, 1}})}}})
                    .has_value());
    CPPL_CHECK(
        !bounded(k::ArithmeticCertificate{k::IntegerSplit{{{7, 1}},
                                                          0,
                                                          k::Box<k::ArithmeticCertificate>{farkas({{2, 1}, {3, 1}})},
                                                          k::Box<k::ArithmeticCertificate>{farkas({{2, 1}, {3, 1}})}}})
             .has_value());
}

CPPL_TEST(overflowing_combinations_are_refused_rather_than_wrapped) {
    k::ArithmeticSystem system;
    system.variables.resize(1);
    const k::Wide huge = k::Wide{1} << 100;
    // huge * x + 1 <= 0 and -huge * x + 1 <= 0 sum to 2 <= 0. Multipliers
    // large enough to overflow while summing are refused, even though the
    // scaled combination would be a contradiction too.
    system.constraints.push_back(k::LinearConstraint{{{0, huge}}, 1});
    system.constraints.push_back(k::LinearConstraint{{{0, -huge}}, 1});
    CPPL_CHECK(k::refutes(system, farkas({{0, 1}, {1, 1}}), {}).has_value());
    CPPL_CHECK(!k::refutes(system, farkas({{0, INT64_MAX}, {1, INT64_MAX}}), {}).has_value());
}

CPPL_TEST(a_certificate_for_a_true_goal_does_not_prove_a_false_one) {
    // x <= 10 does not give x < 5; the certificate that proved x < 20 fails.
    CPPL_CHECK(!bounded(farkas({{2, 1}, {3, 1}}), false).has_value());
}

CPPL_TEST(a_split_examines_both_sides_and_each_must_be_refuted) {
    // Splitting on x - 10 <= 0 | x - 10 >= 1: the first side is refuted by the
    // goal's negation, the second by the fact alone.
    const auto both =
        k::ArithmeticCertificate{k::IntegerSplit{{{0, 1}},
                                                 -10,
                                                 k::Box<k::ArithmeticCertificate>{farkas({{3, 1}, {4, 1}})},
                                                 k::Box<k::ArithmeticCertificate>{farkas({{2, 1}, {4, 1}})}}};
    CPPL_CHECK(bounded(both).has_value());
    // A side left unrefuted fails the whole step.
    const auto one_sided =
        k::ArithmeticCertificate{k::IntegerSplit{{{0, 1}},
                                                 -10,
                                                 k::Box<k::ArithmeticCertificate>{farkas({{3, 1}, {4, 1}})},
                                                 k::Box<k::ArithmeticCertificate>{farkas({{4, 1}})}}};
    CPPL_CHECK(!bounded(one_sided).has_value());
}

CPPL_TEST(arithmetic_facts_must_carry_checked_evidence) {
    const auto premise = comparison(k::PrimOp::LessEqual, kU32, var(0), lit(kU32, 10));
    const auto conclusion = comparison(k::PrimOp::Less, kU32, var(0), lit(kU32, 20));
    // The fact is claimed with reflexivity instead of the hypothesis: x <= 10
    // is not definitionally true, so the step is refused.
    std::vector<k::ArithmeticFact> facts;
    facts.push_back(k::ArithmeticFact{premise, k::Box<k::ProofTerm>{k::ProofTerm::reflexivity()}});
    const auto goal = k::Proposition::for_all(k::Type{kU32}, conclusion);
    const auto proof = k::ProofTerm::forall_introduction(
        k::Type{kU32}, k::ProofTerm::linear_arithmetic(std::move(facts), farkas({{2, 1}, {3, 1}})));
    CPPL_CHECK(!k::check({}, goal, proof, {}).has_value());
    // Nor may a fact name a hypothesis nothing introduced.
    std::vector<k::ArithmeticFact> unintroduced;
    unintroduced.push_back(
        k::ArithmeticFact{premise, k::Box<k::ProofTerm>{k::ProofTerm::hypothesis(k::HypothesisIndex{0})}});
    const auto forged = k::ProofTerm::forall_introduction(
        k::Type{kU32}, k::ProofTerm::linear_arithmetic(std::move(unintroduced), farkas({{2, 1}, {3, 1}})));
    CPPL_CHECK(!k::check({}, goal, forged, {}).has_value());
}

CPPL_TEST(linear_arithmetic_closes_only_equalities) {
    const auto goal = k::Proposition::for_all(k::Type{kU32}, comparison(k::PrimOp::LessEqual, kU32, var(0), var(0)));
    const auto proof = k::ProofTerm::linear_arithmetic({}, farkas({{0, 1}}));
    CPPL_CHECK(!k::check({}, goal, proof, {}).has_value());
}

CPPL_TEST(wrapping_is_part_of_the_system) {
    // x + 1 > x is false for the largest u32, so it has no refutation: the
    // system contains that witness. Given x < 10, it holds.
    const auto x = var(0);
    const auto successor = add(kU32, x, lit(kU32, 1));
    const auto system = k::arithmetic_system({}, {}, comparison(k::PrimOp::Greater, kU32, successor, x), {});
    CPPL_CHECK(system.has_value());
    // One value variable for x, one wrap variable for x + 1.
    CPPL_CHECK_EQ(system->variables.size(), std::size_t{2});
    CPPL_CHECK(system->variables[1].role == k::VariableRole::Wrap);
    CPPL_CHECK(system->variables[1].lowest == 0);
    CPPL_CHECK(system->variables[1].highest == 1);
}

CPPL_TEST(wide_division_truncates_toward_zero_in_every_sign) {
    const auto check_pair = [](k::Wide numerator, k::Wide denominator, k::Wide quotient, k::Wide rest) {
        CPPL_CHECK(k::divide(numerator, denominator) == quotient);
        CPPL_CHECK(k::remainder(numerator, denominator) == rest);
    };
    check_pair(7, 2, 3, 1);
    check_pair(-7, 2, -3, -1);
    check_pair(7, -2, -3, 1);
    check_pair(-7, -2, 3, -1);
    check_pair(6, 3, 2, 0);
    check_pair(1, 4, 0, 1);
    check_pair(0, 5, 0, 0);

    // Above 64 bits, where the target may have no division helper of its own.
    const k::Wide big = (k::Wide{1} << 100) + 5;
    check_pair(big, k::Wide{1} << 100, 1, 5);
    check_pair(k::Wide{5} << 100, 5, k::Wide{1} << 100, 0);
    check_pair(-big, k::Wide{3} << 101, 0, -big);

    const k::Wide least = -(k::Wide{1} << 126) * 2;
    check_pair(least, 2, -(k::Wide{1} << 126), 0);
    check_pair(least + 1, -1, -(least + 1), 0);
}
