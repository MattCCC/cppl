#include "contradiction.hpp"

#include "cppl/kernel/linear.hpp"
#include "cppl/refutation/refute.hpp"

#include <span>
#include <utility>
#include <variant>

namespace cppl::obligations::detail {

namespace {

std::expected<kernel::ArithmeticSystem, Unestablished> state(const kernel::Context& context,
                                                             std::span<const kernel::Proposition> facts,
                                                             const kernel::Proposition& goal) {
    auto system = kernel::arithmetic_system(context, facts, goal, kernel::CoreLimits{});
    if (!system) {
        return std::unexpected(Unestablished{Unestablished::Kind::Unreadable,
                                             kernel::describe(system.error().kind) + ": " + system.error().detail});
    }
    return std::move(*system);
}

// Evidence of the equality `goal` from `absurd`. The fact `0 == 1` alone leaves
// a constraint no integer satisfies, so the system is refuted whatever the goal
// contributes.
std::expected<kernel::ProofTerm, Unestablished> close_equality(const kernel::Context& context, kernel::ProofTerm absurd,
                                                               const kernel::Proposition& goal) {
    const kernel::Proposition only[] = {absurdity()};
    auto system = state(context, only, goal);
    if (!system) {
        return std::unexpected(std::move(system.error()));
    }
    auto certificate = refutation::refute(*system);
    if (!certificate) {
        return std::unexpected(
            Unestablished{Unestablished::Kind::NotFound, "no refutation of " + kernel::describe(goal) + " from " +
                                                             kernel::describe(absurdity()) + " was found"});
    }
    std::vector<kernel::ArithmeticFact> premise;
    premise.push_back(kernel::ArithmeticFact{absurdity(), kernel::Box<kernel::ProofTerm>{std::move(absurd)}});
    return kernel::ProofTerm::linear_arithmetic(std::move(premise), std::move(*certificate));
}

// Evidence of `goal` where hypothesis `index` is evidence of `absurdity()`.
//
// The goal is taken apart by the introduction rules it calls for. The
// hypothesis stays evidence of the same proposition underneath a binder,
// because `absurdity()` mentions no variable, and it moves one position
// further out underneath each premise introduced here.
std::expected<kernel::ProofTerm, Unestablished> close(const kernel::Context& context, const kernel::Proposition& goal,
                                                      std::uint32_t index) {
    if (std::holds_alternative<kernel::Eq>(goal.node)) {
        return close_equality(context, kernel::ProofTerm::hypothesis(kernel::HypothesisIndex{index}), goal);
    }
    if (const auto* quantified = std::get_if<kernel::Forall>(&goal.node)) {
        auto body = close(context, *quantified->body, index);
        if (!body) {
            return body;
        }
        return kernel::ProofTerm::forall_introduction(quantified->binder, std::move(*body));
    }
    if (const auto* implication = std::get_if<kernel::Implies>(&goal.node)) {
        auto conclusion = close(context, *implication->conclusion, index + 1);
        if (!conclusion) {
            return conclusion;
        }
        return kernel::ProofTerm::implication_introduction(*implication->premise, std::move(*conclusion));
    }
    if (const auto* conjunction = std::get_if<kernel::And>(&goal.node)) {
        auto left = close(context, *conjunction->left, index);
        if (!left) {
            return left;
        }
        auto right = close(context, *conjunction->right, index);
        if (!right) {
            return right;
        }
        return kernel::ProofTerm::conjunction_introduction(std::move(*left), std::move(*right));
    }
    const auto& disjunction = std::get<kernel::Or>(goal.node);
    auto left = close(context, *disjunction.left, index);
    if (!left) {
        return left;
    }
    return kernel::ProofTerm::disjunction_introduction(std::move(*left), false);
}

} // namespace

kernel::Proposition absurdity() {
    return kernel::Proposition::equality(kernel::Type{kernel::kBoolean}, kernel::Term::literal(kernel::kBoolean, 0),
                                         kernel::Term::literal(kernel::kBoolean, 1));
}

bool arithmetic_fact(const kernel::Context& context, const kernel::Proposition& proposition) {
    if (!std::holds_alternative<kernel::Eq>(proposition.node)) {
        return false;
    }
    const kernel::Proposition only[] = {proposition};
    return kernel::arithmetic_system(context, only, absurdity(), kernel::CoreLimits{}).has_value();
}

std::expected<kernel::ProofTerm, Unestablished> refute_facts(const kernel::Context& context,
                                                             std::vector<kernel::ArithmeticFact> facts) {
    std::vector<kernel::Proposition> stated;
    stated.reserve(facts.size());
    for (const kernel::ArithmeticFact& fact : facts) {
        stated.push_back(fact.proposition);
    }
    auto system = state(context, stated, absurdity());
    if (!system) {
        return std::unexpected(std::move(system.error()));
    }
    auto certificate = refutation::refute(*system);
    if (!certificate) {
        return std::unexpected(Unestablished{Unestablished::Kind::NotFound, {}});
    }
    return kernel::ProofTerm::linear_arithmetic(std::move(facts), std::move(*certificate));
}

std::expected<kernel::ProofTerm, Unestablished> from_absurdity(const kernel::Context& context, kernel::ProofTerm absurd,
                                                               const kernel::Proposition& goal) {
    if (std::holds_alternative<kernel::Eq>(goal.node)) {
        return close_equality(context, std::move(absurd), goal);
    }
    // A goal with structure is closed underneath the assumption of
    // `absurdity()`, which the evidence then discharges. Introducing the goal's
    // own premises would otherwise renumber every hypothesis `absurd` names.
    auto closed = close(context, goal, 0);
    if (!closed) {
        return closed;
    }
    const kernel::Proposition from = kernel::Proposition::implication(absurdity(), goal);
    return kernel::ProofTerm::implication_elimination(
        from, kernel::ProofTerm::implication_introduction(absurdity(), std::move(*closed)), std::move(absurd));
}

} // namespace cppl::obligations::detail
