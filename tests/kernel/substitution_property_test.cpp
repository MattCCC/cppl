// Finite-model oracles for de Bruijn substitution. Expected meanings are
// evaluated in environments; they are not computed by another substitution.
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

#include <cstdint>
#include <ranges>
#include <utility>
#include <variant>
#include <vector>

namespace {
namespace k = cppl::kernel;
const auto type = k::Type::integer(2, k::Signedness::Unsigned);

struct Random {
    std::uint64_t state = 0x23a6719e5d4b8c01ULL;
    std::uint32_t below(std::uint32_t bound) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<std::uint32_t>(state % bound);
    }
};

k::Term var(std::uint32_t index) {
    return k::Term::variable(k::VarIndex{index});
}
k::Term lit(std::uint32_t value) {
    return k::Term::literal(type.integer_type(), value);
}
k::Proposition eq(k::Term lhs, k::Term rhs) {
    return k::Proposition::equality(type, std::move(lhs), std::move(rhs));
}

k::Term term(Random& random, std::uint32_t variables, unsigned depth) {
    const auto choice = random.below(depth == 0 ? 2 : 5);
    if (choice == 0 && variables != 0)
        return var(random.below(variables));
    if (choice <= 1)
        return lit(random.below(4));
    const k::PrimOp ops[] = {k::PrimOp::AddWrap, k::PrimOp::SubWrap, k::PrimOp::MulWrap};
    auto lhs = term(random, variables, depth - 1);
    auto rhs = term(random, variables, depth - 1);
    return k::Term::primitive(ops[choice - 2], type.integer_type(), {lhs, rhs});
}

k::Proposition proposition(Random& random, std::uint32_t variables, unsigned depth) {
    const auto choice = random.below(depth == 0 ? 1 : 5);
    if (choice == 1) {
        return k::Proposition::for_all(type, proposition(random, variables + 1, depth - 1));
    }
    if (choice == 2) {
        auto premise = proposition(random, variables, depth - 1);
        auto conclusion = proposition(random, variables, depth - 1);
        return k::Proposition::implication(std::move(premise), std::move(conclusion));
    }
    if (choice == 3) {
        auto left = proposition(random, variables, depth - 1);
        auto right = proposition(random, variables, depth - 1);
        return k::Proposition::conjunction(std::move(left), std::move(right));
    }
    if (choice == 4) {
        auto left = proposition(random, variables, depth - 1);
        auto right = proposition(random, variables, depth - 1);
        return k::Proposition::disjunction(std::move(left), std::move(right));
    }
    auto lhs = term(random, variables, 2);
    auto rhs = term(random, variables, 2);
    return eq(std::move(lhs), std::move(rhs));
}

using Environment = std::vector<std::uint32_t>;

std::uint32_t evaluate(const k::Term& value, const Environment& environment) {
    if (const auto* variable = std::get_if<k::Var>(&value.node)) {
        CPPL_CHECK(variable->index.value < environment.size());
        return environment[environment.size() - 1 - variable->index.value];
    }
    if (const auto* literal = std::get_if<k::Literal>(&value.node)) {
        return static_cast<std::uint32_t>(literal->value);
    }
    const auto& primitive = std::get<k::Prim>(value.node);
    const auto a = evaluate(primitive.arguments[0], environment);
    const auto b = evaluate(primitive.arguments[1], environment);
    switch (primitive.op) {
        case k::PrimOp::AddWrap:
            return (a + b) % 4;
        case k::PrimOp::SubWrap:
            return (a + 4 - b) % 4;
        case k::PrimOp::MulWrap:
            return (a * b) % 4;
        default:
            CPPL_CHECK(false);
            return 0;
    }
}

bool evaluate(const k::Proposition& value, Environment environment) {
    if (const auto* equality = std::get_if<k::Eq>(&value.node)) {
        return evaluate(equality->lhs, environment) == evaluate(equality->rhs, environment);
    }
    if (const auto* implication = std::get_if<k::Implies>(&value.node)) {
        return !evaluate(*implication->premise, environment) || evaluate(*implication->conclusion, environment);
    }
    if (const auto* conjunction = std::get_if<k::And>(&value.node)) {
        return evaluate(*conjunction->left, environment) && evaluate(*conjunction->right, environment);
    }
    if (const auto* disjunction = std::get_if<k::Or>(&value.node)) {
        return evaluate(*disjunction->left, environment) || evaluate(*disjunction->right, environment);
    }
    const auto& forall = std::get<k::Forall>(value.node);
    environment.push_back(0);
    for (std::uint32_t x = 0; x < 4; ++x) {
        environment.back() = x;
        if (!evaluate(*forall.body, environment))
            return false;
    }
    return true;
}

k::ProofTerm reflexive_shape(const k::Proposition& goal) {
    if (const auto* forall = std::get_if<k::Forall>(&goal.node)) {
        return k::ProofTerm::forall_introduction(forall->binder, reflexive_shape(*forall->body));
    }
    if (const auto* implies = std::get_if<k::Implies>(&goal.node)) {
        return k::ProofTerm::implication_introduction(*implies->premise, reflexive_shape(*implies->conclusion));
    }
    if (const auto* conjunction = std::get_if<k::And>(&goal.node)) {
        return k::ProofTerm::conjunction_introduction(reflexive_shape(*conjunction->left),
                                                      reflexive_shape(*conjunction->right));
    }
    // One side is enough for a disjunction, and this shape always offers the
    // left one, so a goal whose left side is false is rejected even when the
    // right side holds.
    if (const auto* disjunction = std::get_if<k::Or>(&goal.node)) {
        return k::ProofTerm::disjunction_introduction(reflexive_shape(*disjunction->left), false);
    }
    return k::ProofTerm::reflexivity();
}
} // namespace

CPPL_TEST(substitution_preserves_meaning_at_every_tested_binder_depth) {
    Random random;
    for (unsigned sample = 0; sample < 400; ++sample) {
        const auto depth = random.below(3);
        const auto body = proposition(random, 3 + depth, 3);
        const auto value = term(random, 2, 3);
        const auto instantiated = k::instantiate(body, value, depth);
        const auto raw = term(random, 3 + depth, 3);
        const auto raw_instance = k::instantiate(raw, value, depth);
        for (std::uint32_t a = 0; a < 4; ++a) {
            for (std::uint32_t b = 0; b < 4; ++b) {
                Environment outer{a, b};
                Environment before = outer;
                before.push_back(evaluate(value, outer));
                Environment after = outer;
                for (std::uint32_t inner = 0; inner < depth; ++inner) {
                    before.push_back((a + b + inner) % 4);
                    after.push_back(before.back());
                }
                CPPL_CHECK_EQ(evaluate(body, before), evaluate(instantiated, after));
                CPPL_CHECK_EQ(evaluate(raw, before), evaluate(raw_instance, after));
            }
        }
    }
}

CPPL_TEST(shifting_preserves_free_variables_below_nested_propositions) {
    Random random;
    for (unsigned sample = 0; sample < 400; ++sample) {
        const auto cutoff = random.below(3);
        const auto amount = 1 + random.below(3);
        const auto body = proposition(random, 3, 3);
        const auto shifted = k::shift(body, amount, cutoff);
        const auto raw = term(random, 3, 3);
        const auto shifted_raw = k::shift(raw, amount, cutoff);
        for (std::uint32_t assignment = 0; assignment < 64; ++assignment) {
            const Environment before{assignment % 4, (assignment / 4) % 4, assignment / 16};
            Environment after = before;
            after.insert(after.end() - cutoff, amount, (assignment + 1) % 4);
            CPPL_CHECK_EQ(evaluate(body, before), evaluate(shifted, after));
            CPPL_CHECK_EQ(evaluate(raw, before), evaluate(shifted_raw, after));
        }
        CPPL_CHECK_EQ(k::shift(body, 0), body);
        CPPL_CHECK_EQ(k::shift(k::shift(body, 2), 3), k::shift(body, 5));
    }
}

CPPL_TEST(repeated_instantiation_agrees_with_direct_environment_evaluation) {
    Random random;
    for (unsigned sample = 0; sample < 300; ++sample) {
        const auto body = proposition(random, 3, 3);
        for (std::uint32_t assignment = 0; assignment < 64; ++assignment) {
            const Environment values{assignment % 4, (assignment / 4) % 4, assignment / 16};
            auto instance = body;
            for (unsigned int value : std::views::reverse(values)) {
                instance = k::instantiate(instance, lit(value));
            }
            CPPL_CHECK_EQ(evaluate(body, values), evaluate(instance, {}));
        }
    }
}

CPPL_TEST(generated_quantified_proofs_are_checked_against_a_finite_model) {
    Random random;
    unsigned accepted = 0;
    unsigned rejected = 0;
    // Conjunctive goals hold only when both sides do, so more draws are taken
    // to keep both outcomes well represented.
    for (unsigned sample = 0; sample < 2400; ++sample) {
        const auto goal = k::Proposition::for_all(type, proposition(random, 1, 3));
        const auto result = k::check({}, goal, reflexive_shape(goal), {});
        if (result) {
            ++accepted;
            CPPL_CHECK(evaluate(goal, {}));
        } else {
            ++rejected;
        }
        // A free hypothesis never gains a premise from a preceding check.
        CPPL_CHECK(!k::check({}, eq(lit(0), lit(1)), k::ProofTerm::hypothesis(k::HypothesisIndex{0}), {}).has_value());
    }
    CPPL_CHECK(accepted > 50);
    CPPL_CHECK(rejected > 50);
}

CPPL_TEST(transport_beneath_many_binders_preserves_the_outer_equality) {
    const auto premise = eq(var(1), var(0));
    for (std::uint32_t depth = 0; depth < 40; ++depth) {
        // forall a b. a=b -> forall z... . a=b, transported through C[-]=(-=b).
        auto motive = eq(var(depth), var(depth + 1));
        auto conclusion = eq(var(depth + 1), var(depth));
        auto captured = eq(var(depth), var(depth));
        auto evidence = k::ProofTerm::reflexivity();
        for (std::uint32_t binder = 0; binder < depth; ++binder) {
            motive = k::Proposition::for_all(type, std::move(motive));
            conclusion = k::Proposition::for_all(type, std::move(conclusion));
            captured = k::Proposition::for_all(type, std::move(captured));
            evidence = k::ProofTerm::forall_introduction(type, std::move(evidence));
        }
        const auto transport = k::ProofTerm::equality_elimination(
            type, var(1), var(0), motive, k::ProofTerm::hypothesis(k::HypothesisIndex{0}), evidence);
        const auto proof = k::ProofTerm::forall_introduction(
            type, k::ProofTerm::forall_introduction(type, k::ProofTerm::implication_introduction(premise, transport)));
        const auto close = [&](k::Proposition body) {
            return k::Proposition::for_all(
                type, k::Proposition::for_all(type, k::Proposition::implication(premise, std::move(body))));
        };
        CPPL_CHECK(k::check({}, close(conclusion), proof, {}).has_value());
        CPPL_CHECK(!k::check({}, close(captured), proof, {}).has_value());
    }
}
