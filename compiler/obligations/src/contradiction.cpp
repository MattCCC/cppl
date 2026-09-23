#include "contradiction.hpp"

#include "cppl/kernel/box.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/linear.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/refutation/refute.hpp"

#include <expected>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations::detail {

bool arithmetic_fact(const kernel::Context& context, const kernel::Proposition& proposition) {
    if (!std::holds_alternative<kernel::Eq>(proposition.node)) {
        return false;
    }
    const kernel::Proposition only[] = {proposition};
    return kernel::arithmetic_system(context, only, kernel::Proposition::falsity(), kernel::CoreLimits{}).has_value();
}

std::expected<kernel::ProofTerm, Unestablished> refute(const kernel::Context& context, kernel::ArithmeticFact named,
                                                       std::span<const Standing> standing) {
    const kernel::CoreLimits limits{};
    std::vector<kernel::ArithmeticFact> facts;
    facts.push_back(std::move(named));
    const auto state = [&](auto&& self, const kernel::Proposition& proposition, kernel::ProofTerm term) -> void {
        if (facts.size() >= limits.max_arithmetic_facts) {
            return;
        }
        if (const auto* conjunction = std::get_if<kernel::And>(&proposition.node)) {
            self(self, *conjunction->left, kernel::ProofTerm::conjunction_elimination(proposition, term, false));
            self(self, *conjunction->right,
                 kernel::ProofTerm::conjunction_elimination(proposition, std::move(term), true));
            return;
        }
        if (arithmetic_fact(context, proposition)) {
            facts.push_back(kernel::ArithmeticFact{proposition, kernel::Box<kernel::ProofTerm>{std::move(term)}});
        }
    };
    for (const Standing& premise : standing) {
        state(state, premise.proposition, premise.evidence);
    }

    std::vector<kernel::Proposition> stated;
    stated.reserve(facts.size());
    for (const kernel::ArithmeticFact& fact : facts) {
        stated.push_back(fact.proposition);
    }
    auto system = kernel::arithmetic_system(context, stated, kernel::Proposition::falsity(), limits);
    if (!system) {
        return std::unexpected(Unestablished{Unestablished::Kind::Unreadable,
                                             kernel::describe(system.error().kind) + ": " + system.error().detail});
    }
    auto certificate = refutation::refute(*system);
    if (!certificate) {
        return std::unexpected(Unestablished{Unestablished::Kind::NotFound, {}});
    }
    return kernel::ProofTerm::linear_arithmetic(std::move(facts), std::move(*certificate));
}

} // namespace cppl::obligations::detail
