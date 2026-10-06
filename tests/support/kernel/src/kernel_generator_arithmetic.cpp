// Derivations by linear arithmetic, with certificates proposed, drawn at
// random or corrupted, and the goals handed to automation.

#include "cppl/kernel/box.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/linear.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/refutation/refute.hpp"
#include "kernel_generator_detail.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::testing::kernel_generator {

using detail::Derivation;
using detail::Generator;
using detail::Scope;

std::optional<k::ProofTerm> Generator::arithmetic_from_scope(const Scope& scope, const k::Proposition& goal) {
    std::vector<k::ArithmeticFact> facts;
    std::vector<k::Proposition> stated;
    for (std::size_t position = 0; position < scope.hypotheses.size(); ++position) {
        if (!scope.hypotheses[position].usable) {
            continue;
        }
        k::Proposition fact = available(scope, position);
        if (!std::holds_alternative<k::Eq>(fact.node) || !std::get<k::Eq>(fact.node).type.is_integer() || one_in(4)) {
            continue;
        }
        stated.push_back(fact);
        facts.push_back(k::ArithmeticFact{std::move(fact), k::Box<k::ProofTerm>{use(scope, position)}});
    }
    auto certificate = propose_certificate(scope.locals, stated, goal);
    if (!certificate) {
        return std::nullopt;
    }
    return k::ProofTerm::linear_arithmetic(std::move(facts), std::move(*certificate));
}

std::optional<k::ArithmeticCertificate> Generator::propose_certificate(std::span<const k::Type> locals,
                                                                       std::span<const k::Proposition> facts,
                                                                       const k::Proposition& goal) {
    const auto system = k::arithmetic_system(context_, facts, goal, k::CoreLimits{}, locals);
    if (!system) {
        return std::nullopt;
    }
    auto certificate = refutation::refute(*system);
    if (!certificate) {
        if (one_in(3)) {
            return random_certificate(system->constraints.size(), system->variables.size(), system->disjunctions.size(),
                                      2);
        }
        return std::nullopt;
    }
    if (perturb()) {
        return corrupt(*certificate, system->constraints.size());
    }
    return certificate;
}

k::ArithmeticCertificate Generator::random_certificate(std::size_t constraints, std::size_t variables,
                                                       std::size_t disjunctions, unsigned depth) {
    const auto count = [](std::size_t n) {
        return static_cast<std::uint32_t>(n + 2);
    };
    switch (depth == 0 ? 0 : below(3)) {
        case 1: {
            std::vector<std::pair<std::uint32_t, std::int64_t>> terms;
            terms.emplace_back(below(count(variables)), static_cast<std::int64_t>(below(5)) - 2);
            const auto constant = static_cast<std::int64_t>(below(5)) - 2;
            auto at_most_zero = random_certificate(constraints + 1, variables, disjunctions, depth - 1);
            auto at_least_one = random_certificate(constraints + 1, variables, disjunctions, depth - 1);
            return k::ArithmeticCertificate{k::IntegerSplit{std::move(terms), constant,
                                                            k::Box<k::ArithmeticCertificate>{std::move(at_most_zero)},
                                                            k::Box<k::ArithmeticCertificate>{std::move(at_least_one)}}};
        }
        case 2: {
            const std::uint32_t disjunction = below(count(disjunctions));
            auto first = random_certificate(constraints + 1, variables, disjunctions, depth - 1);
            auto second = random_certificate(constraints + 1, variables, disjunctions, depth - 1);
            return k::ArithmeticCertificate{k::DisjunctionCases{disjunction,
                                                                k::Box<k::ArithmeticCertificate>{std::move(first)},
                                                                k::Box<k::ArithmeticCertificate>{std::move(second)}}};
        }
        default: {
            k::FarkasSum sum;
            std::uint32_t position = below(3);
            const std::uint32_t used = 1 + below(4);
            for (std::uint32_t term = 0; term < used; ++term) {
                sum.multipliers.emplace_back(position, Wide{below(4)});
                position += 1 + below(3);
            }
            (void)constraints;
            return k::ArithmeticCertificate{std::move(sum)};
        }
    }
}

k::ArithmeticCertificate Generator::corrupt(const k::ArithmeticCertificate& certificate, std::size_t constraints) {
    k::ArithmeticCertificate result = certificate;
    if (auto* sum = std::get_if<k::FarkasSum>(&result.node)) {
        if (sum->multipliers.empty()) {
            return result;
        }
        auto& chosen = sum->multipliers[below(static_cast<std::uint32_t>(sum->multipliers.size()))];
        switch (below(4)) {
            case 0:
                chosen.second += 1;
                break;
            case 1:
                chosen.second = chosen.second > 1 ? chosen.second - 1 : chosen.second + 2;
                break;
            case 2:
                sum->multipliers.erase(
                    sum->multipliers.begin() +
                    static_cast<std::ptrdiff_t>(below(static_cast<std::uint32_t>(sum->multipliers.size()))));
                break;
            default:
                chosen.first = static_cast<std::uint32_t>((chosen.first + 1) % (constraints + 1));
                break;
        }
        return result;
    }
    if (auto* split = std::get_if<k::IntegerSplit>(&result.node)) {
        if (one_in(2)) {
            split->constant += 1;
        } else {
            split->at_most_zero = k::Box<k::ArithmeticCertificate>{corrupt(*split->at_most_zero, constraints + 1)};
        }
        return result;
    }
    auto& cases = std::get<k::DisjunctionCases>(result.node);
    if (one_in(2)) {
        std::swap(cases.first, cases.second);
    } else {
        cases.first = k::Box<k::ArithmeticCertificate>{corrupt(*cases.first, constraints + 1)};
    }
    return result;
}

k::ProofTerm Generator::restate(const Scope& /*scope*/, const k::Proposition& premise, const k::Proposition& goal) {
    const auto& equality = std::get<k::Eq>(premise.node);
    const k::Proposition motive = abstract(k::shift(goal, 1), k::shift(equality.lhs, 1), 0);
    return k::ProofTerm::implication_introduction(
        premise, k::ProofTerm::equality_elimination(equality.type, equality.lhs, equality.rhs, motive,
                                                    k::ProofTerm::hypothesis(k::HypothesisIndex{0}),
                                                    k::ProofTerm::reflexivity()));
}

Derivation Generator::linear_arithmetic(Scope& scope) {
    const std::size_t mark = scope.hypotheses.size();
    const std::uint32_t supposed = below(3);
    for (std::uint32_t index = 0; index < supposed; ++index) {
        suppose(scope, comparison_proposition(scope.locals, 2));
    }
    k::Proposition goal = one_in(5) ? k::Proposition::falsity() : comparison_proposition(scope.locals, 2);
    auto proof = arithmetic_from_scope(scope, goal);
    if (!proof) {
        return discharge(scope, mark, reflexivity(scope, 0));
    }
    return discharge(scope, mark, Derivation{std::move(goal), std::move(*proof)});
}

Derivation Generator::arithmetic() {
    Scope scope;
    const std::uint32_t binders = 1 + below(3);
    for (std::uint32_t index = 0; index < binders; ++index) {
        scope.locals.push_back(one_in(6)
                                   ? as_type(k::IntType{8, one_in(2) ? k::Signedness::Signed : k::Signedness::Unsigned})
                                   : as_type(small_integer()));
    }
    const std::uint32_t facts = below(4);
    for (std::uint32_t index = 0; index < facts; ++index) {
        suppose(scope, comparison_proposition(scope.locals, 2));
    }
    k::Proposition goal = one_in(6) ? k::Proposition::falsity() : comparison_proposition(scope.locals, 2);
    std::vector<k::ArithmeticFact> used;
    std::vector<k::Proposition> stated;
    for (std::size_t position = 0; position < scope.hypotheses.size(); ++position) {
        if (one_in(6)) {
            continue;
        }
        stated.push_back(scope.hypotheses[position].proposition);
        used.push_back(
            k::ArithmeticFact{scope.hypotheses[position].proposition, k::Box<k::ProofTerm>{use(scope, position)}});
    }
    auto certificate = propose_certificate(scope.locals, stated, goal);
    k::ProofTerm proof = certificate ? k::ProofTerm::linear_arithmetic(std::move(used), std::move(*certificate))
                                     : k::ProofTerm::reflexivity();
    Derivation derived = discharge(scope, 0, Derivation{std::move(goal), std::move(proof)});
    while (!scope.locals.empty()) {
        const k::Type bound = scope.locals.back();
        scope.locals.pop_back();
        derived.proposition = k::Proposition::for_all(bound, std::move(derived.proposition));
        derived.proof = k::ProofTerm::forall_introduction(bound, std::move(derived.proof));
    }
    return derived;
}

k::Proposition Generator::automation_goal() {
    std::vector<k::Type> locals;
    const std::uint32_t binders = below(3);
    locals.reserve(binders);
    for (std::uint32_t index = 0; index < binders; ++index) {
        locals.push_back(as_type(small_integer()));
    }
    std::vector<k::Proposition> premises;
    const std::uint32_t count = below(3);
    premises.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        premises.push_back(comparison_proposition(locals, 2));
    }
    k::Proposition goal =
        one_in(4) ? k::Proposition::disjunction(comparison_proposition(locals, 2), comparison_proposition(locals, 2))
                  : comparison_proposition(locals, 2);
    for (const k::Proposition& premise : std::ranges::reverse_view(premises)) {
        goal = k::Proposition::implication(premise, std::move(goal));
    }
    while (!locals.empty()) {
        goal = k::Proposition::for_all(locals.back(), std::move(goal));
        locals.pop_back();
    }
    return goal;
}

} // namespace cppl::testing::kernel_generator
