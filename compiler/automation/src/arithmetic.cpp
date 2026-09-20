#include "arithmetic.hpp"

#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/obligations/obligation.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::automation {

namespace {

namespace k = kernel;
using Wide = k::Wide;

constexpr std::size_t kMaxRows = 4096;
constexpr std::size_t kMaxSearchNodes = 512;
constexpr Wide kMaxPinnedRange = 16;
constexpr std::size_t kMaxRewrites = 32;
constexpr std::size_t kMaxDerivations = 48;

[[nodiscard]] bool add(Wide& into, Wide value) {
    return !__builtin_add_overflow(into, value, &into);
}

[[nodiscard]] bool multiply(Wide lhs, Wide rhs, Wide& out) {
    return !__builtin_mul_overflow(lhs, rhs, &out);
}

Wide magnitude(Wide value) {
    return value < 0 && value != std::numeric_limits<Wide>::min() ? -value : value;
}

Wide gcd(Wide lhs, Wide rhs) {
    lhs = magnitude(lhs);
    rhs = magnitude(rhs);
    while (rhs != 0) {
        const Wide rest = k::remainder(lhs, rhs);
        lhs = rhs;
        rhs = rest;
    }
    return lhs;
}

// A constraint during elimination, with the nonnegative multiples of the
// original constraints it was combined from.
struct Row {
    std::vector<Wide> coefficients;
    Wide constant = 0;
    std::vector<Wide> origin;
};

bool vanishes(const Row& row) {
    return std::ranges::all_of(row.coefficients, [](Wide value) { return value == 0; });
}

void reduce(Row& row) {
    Wide divisor = row.constant;
    for (const Wide value : row.coefficients)
        divisor = gcd(divisor, value);
    for (const Wide value : row.origin)
        divisor = gcd(divisor, value);
    if (divisor <= 1) {
        return;
    }
    for (Wide& value : row.coefficients)
        value = k::divide(value, divisor);
    for (Wide& value : row.origin)
        value = k::divide(value, divisor);
    row.constant = k::divide(row.constant, divisor);
}

// Fourier-Motzkin elimination. Returns the multiples of `active` that sum to a
// positive constant, if the rational relaxation is already infeasible.
std::optional<std::vector<Wide>> eliminate(const std::vector<k::LinearConstraint>& active, std::size_t variables) {
    std::vector<Row> rows;
    rows.reserve(active.size());
    for (std::size_t index = 0; index < active.size(); ++index) {
        Row row{std::vector<Wide>(variables, 0), active[index].constant, std::vector<Wide>(active.size(), 0)};
        for (const auto& [variable, coefficient] : active[index].terms) {
            row.coefficients[variable] = coefficient;
        }
        row.origin[index] = 1;
        rows.push_back(std::move(row));
    }

    std::vector<bool> gone(variables, false);
    while (true) {
        for (const Row& row : rows) {
            if (vanishes(row) && row.constant > 0) {
                return row.origin;
            }
        }

        std::optional<std::size_t> chosen;
        std::size_t cheapest = 0;
        for (std::size_t variable = 0; variable < variables; ++variable) {
            if (gone[variable]) {
                continue;
            }
            std::size_t positive = 0;
            std::size_t negative = 0;
            for (const Row& row : rows) {
                positive += row.coefficients[variable] > 0 ? 1u : 0u;
                negative += row.coefficients[variable] < 0 ? 1u : 0u;
            }
            if (positive + negative == 0) {
                gone[variable] = true;
                continue;
            }
            const std::size_t cost = positive * negative;
            if (!chosen || cost < cheapest) {
                chosen = variable;
                cheapest = cost;
            }
        }
        if (!chosen) {
            return std::nullopt;
        }
        const std::size_t variable = *chosen;
        gone[variable] = true;

        std::vector<const Row*> positive;
        std::vector<const Row*> negative;
        std::vector<Row> next;
        for (const Row& row : rows) {
            if (row.coefficients[variable] > 0) {
                positive.push_back(&row);
            } else if (row.coefficients[variable] < 0) {
                negative.push_back(&row);
            } else {
                next.push_back(row);
            }
        }
        // A variable bounded on one side only can always be chosen to satisfy
        // the rows that mention it, so those rows say nothing more.
        for (const Row* upper : positive) {
            for (const Row* lower : negative) {
                // Each row is scaled by the other's coefficient over their
                // common divisor, the least that cancels the variable.
                const Wide common = gcd(upper->coefficients[variable], lower->coefficients[variable]);
                const Wide a = k::divide(upper->coefficients[variable], common);
                const Wide b = k::divide(-lower->coefficients[variable], common);
                Row combined{std::vector<Wide>(variables, 0), 0, std::vector<Wide>(active.size(), 0)};
                bool fits = true;
                for (std::size_t index = 0; fits && index < variables; ++index) {
                    Wide left = 0;
                    Wide right = 0;
                    fits = multiply(b, upper->coefficients[index], left) &&
                           multiply(a, lower->coefficients[index], right) && add(left, right);
                    combined.coefficients[index] = left;
                }
                for (std::size_t index = 0; fits && index < active.size(); ++index) {
                    Wide left = 0;
                    Wide right = 0;
                    fits = multiply(b, upper->origin[index], left) && multiply(a, lower->origin[index], right) &&
                           add(left, right);
                    combined.origin[index] = left;
                }
                Wide left = 0;
                Wide right = 0;
                fits = fits && multiply(b, upper->constant, left) && multiply(a, lower->constant, right) &&
                       add(left, right);
                if (!fits) {
                    return std::nullopt;
                }
                combined.constant = left;
                reduce(combined);
                if (vanishes(combined) && combined.constant <= 0) {
                    continue;
                }
                next.push_back(std::move(combined));
                if (next.size() > kMaxRows) {
                    return std::nullopt;
                }
            }
        }

        // Of rows with the same variable part, the one with the largest
        // constant implies the others.
        std::map<std::vector<Wide>, std::size_t> strongest;
        rows.clear();
        for (Row& row : next) {
            const auto [entry, inserted] = strongest.try_emplace(row.coefficients, rows.size());
            if (inserted) {
                rows.push_back(std::move(row));
            } else if (row.constant > rows[entry->second].constant) {
                rows[entry->second] = std::move(row);
            }
        }
    }
}

class Search {
  public:
    explicit Search(const k::ArithmeticSystem& system)
        : system_(system),
          active_(system.constraints),
          split_(system.disjunctions.size(), false),
          pinned_(system.variables.size(), false) {}

    std::optional<k::ArithmeticCertificate> solve() {
        if (++nodes_ > kMaxSearchNodes) {
            return std::nullopt;
        }
        if (auto multipliers = eliminate(active_, system_.variables.size())) {
            return farkas(*multipliers);
        }
        for (std::size_t index = 0; index < system_.disjunctions.size(); ++index) {
            if (split_[index]) {
                continue;
            }
            split_[index] = true;
            auto first = descend(system_.disjunctions[index][0]);
            std::optional<k::ArithmeticCertificate> second;
            if (first) {
                second = descend(system_.disjunctions[index][1]);
            }
            split_[index] = false;
            if (!first || !second) {
                return std::nullopt;
            }
            return k::ArithmeticCertificate{k::DisjunctionCases{static_cast<std::uint32_t>(index),
                                                                k::Box<k::ArithmeticCertificate>{std::move(*first)},
                                                                k::Box<k::ArithmeticCertificate>{std::move(*second)}}};
        }
        for (std::size_t index = 0; index < system_.variables.size(); ++index) {
            const k::ArithmeticVariable& variable = system_.variables[index];
            if (variable.role != k::VariableRole::Wrap || pinned_[index] || variable.lowest > variable.highest ||
                variable.highest - variable.lowest >= kMaxPinnedRange) {
                continue;
            }
            pinned_[index] = true;
            auto result = pin(static_cast<std::uint32_t>(index), variable.lowest, variable.highest);
            pinned_[index] = false;
            return result;
        }
        return std::nullopt;
    }

  private:
    // The multiple is below its range, or it is each value in turn, or it is
    // above its range. The kernel checks that every case is refuted.
    std::optional<k::ArithmeticCertificate> pin(std::uint32_t variable, Wide lowest, Wide highest) {
        return split(
            variable, lowest - 1, [&] { return solve(); }, [&] { return pin_from(variable, lowest, highest); });
    }

    // Standing: variable >= value.
    std::optional<k::ArithmeticCertificate> pin_from(std::uint32_t variable, Wide value, Wide highest) {
        return split(
            variable, value, [&] { return solve(); },
            [&] { return value >= highest ? solve() : pin_from(variable, value + 1, highest); });
    }

    // variable <= value | variable >= value + 1
    std::optional<k::ArithmeticCertificate> split(
        std::uint32_t variable, Wide value, const std::function<std::optional<k::ArithmeticCertificate>()>& below,
        const std::function<std::optional<k::ArithmeticCertificate>()>& above) {
        if (value > std::numeric_limits<std::int64_t>::max() - 1 ||
            value < std::numeric_limits<std::int64_t>::min() + 1) {
            return std::nullopt;
        }
        k::LinearConstraint at_most{{{variable, Wide{1}}}, -value};
        k::LinearConstraint at_least{{{variable, Wide{-1}}}, value + 1};
        active_.push_back(std::move(at_most));
        auto first = below();
        active_.pop_back();
        if (!first) {
            return std::nullopt;
        }
        active_.push_back(std::move(at_least));
        auto second = above();
        active_.pop_back();
        if (!second) {
            return std::nullopt;
        }
        return k::ArithmeticCertificate{k::IntegerSplit{{{variable, std::int64_t{1}}},
                                                        static_cast<std::int64_t>(-value),
                                                        k::Box<k::ArithmeticCertificate>{std::move(*first)},
                                                        k::Box<k::ArithmeticCertificate>{std::move(*second)}}};
    }

    std::optional<k::ArithmeticCertificate> descend(const k::LinearConstraint& added) {
        active_.push_back(added);
        auto result = solve();
        active_.pop_back();
        return result;
    }

    static std::optional<k::ArithmeticCertificate> farkas(const std::vector<Wide>& multipliers) {
        k::FarkasSum sum;
        for (std::size_t index = 0; index < multipliers.size(); ++index) {
            if (multipliers[index] == 0) {
                continue;
            }
            if (multipliers[index] < 0) {
                return std::nullopt;
            }
            sum.multipliers.emplace_back(static_cast<std::uint32_t>(index), multipliers[index]);
        }
        if (sum.multipliers.empty()) {
            return std::nullopt;
        }
        return k::ArithmeticCertificate{std::move(sum)};
    }

    const k::ArithmeticSystem& system_;
    std::vector<k::LinearConstraint> active_;
    std::vector<bool> split_;
    std::vector<bool> pinned_;
    std::size_t nodes_ = 0;
};

bool occurs(const k::Term& inside, const k::Term& target) {
    if (inside == target) {
        return true;
    }
    if (const auto* call = std::get_if<k::Call>(&inside.node)) {
        return std::ranges::any_of(call->arguments, [&](const k::Term& argument) { return occurs(argument, target); });
    }
    if (const auto* primitive = std::get_if<k::Prim>(&inside.node)) {
        return std::ranges::any_of(primitive->arguments,
                                   [&](const k::Term& argument) { return occurs(argument, target); });
    }
    return false;
}

void variables_of(const k::Term& term, std::set<std::uint32_t>& found) {
    if (const auto* variable = std::get_if<k::Var>(&term.node)) {
        found.insert(variable->index.value);
    } else if (const auto* call = std::get_if<k::Call>(&term.node)) {
        for (const auto& argument : call->arguments)
            variables_of(argument, found);
    } else if (const auto* primitive = std::get_if<k::Prim>(&term.node)) {
        for (const auto& argument : primitive->arguments)
            variables_of(argument, found);
    }
}

struct Premise {
    k::Proposition proposition;
    std::size_t binders = 0;
};

// An equality usable for rewriting, with its evidence at the leaf.
struct Equality {
    k::Type type;
    k::Term lhs;
    k::Term rhs;
    k::ProofTerm evidence;
};

struct Rewrite {
    Equality equality;
    k::Proposition motive;
};

class Prover {
  public:
    Prover(const k::Context& context, bool rewriting) : context_(context), rewriting_(rewriting) {}

    std::optional<k::ProofTerm> prove(const k::Proposition& goal) {
        if (const auto* quantified = std::get_if<k::Forall>(&goal.node)) {
            binders_.push_back(quantified->binder);
            auto body = prove(*quantified->body);
            binders_.pop_back();
            if (!body)
                return std::nullopt;
            return k::ProofTerm::forall_introduction(quantified->binder, std::move(*body));
        }
        if (const auto* implication = std::get_if<k::Implies>(&goal.node)) {
            premises_.push_back(Premise{*implication->premise, binders_.size()});
            auto body = prove(*implication->conclusion);
            premises_.pop_back();
            if (!body)
                return std::nullopt;
            return k::ProofTerm::implication_introduction(*implication->premise, std::move(*body));
        }
        if (const auto* conjunction = std::get_if<k::And>(&goal.node)) {
            auto left = prove(*conjunction->left);
            if (!left)
                return std::nullopt;
            auto right = prove(*conjunction->right);
            if (!right)
                return std::nullopt;
            return k::ProofTerm::conjunction_introduction(std::move(*left), std::move(*right));
        }
        return rewriting_ ? rewrite_then_close(goal) : by_arithmetic(goal);
    }

  private:
    // Only equality leaves enter arithmetic. Every projection from a
    // conjunctive premise carries evidence the kernel checks independently.
    static void append_facts(const k::Proposition& proposition, k::ProofTerm evidence,
                             std::vector<k::ArithmeticFact>& result) {
        if (const auto* conjunction = std::get_if<k::And>(&proposition.node)) {
            append_facts(*conjunction->left, k::ProofTerm::conjunction_elimination(proposition, evidence, false),
                         result);
            append_facts(*conjunction->right,
                         k::ProofTerm::conjunction_elimination(proposition, std::move(evidence), true), result);
        } else if (std::holds_alternative<k::Eq>(proposition.node)) {
            result.push_back(k::ArithmeticFact{proposition, k::Box<k::ProofTerm>{std::move(evidence)}});
        }
    }

    // The premises in scope, restated at the leaf, each with its hypothesis.
    std::vector<k::ArithmeticFact> facts() const {
        std::vector<k::ArithmeticFact> result;
        for (std::size_t index = 0; index < premises_.size(); ++index) {
            const Premise& premise = premises_[index];
            append_facts(
                k::shift(premise.proposition, static_cast<std::uint32_t>(binders_.size() - premise.binders)),
                k::ProofTerm::hypothesis(k::HypothesisIndex{static_cast<std::uint32_t>(premises_.size() - 1 - index)}),
                result);
        }
        return result;
    }

    std::optional<k::ProofTerm> by_arithmetic(const k::Proposition& goal) const {
        if (!std::holds_alternative<k::Eq>(goal.node)) {
            return std::nullopt;
        }
        std::vector<k::ArithmeticFact> used = facts();
        std::vector<k::Proposition> propositions;
        propositions.reserve(used.size());
        for (const auto& fact : used)
            propositions.push_back(fact.proposition);
        const auto system = k::arithmetic_system(context_, propositions, goal, k::CoreLimits{});
        if (!system) {
            return std::nullopt;
        }
        Search search(*system);
        auto certificate = search.solve();
        if (!certificate) {
            return std::nullopt;
        }
        return k::ProofTerm::linear_arithmetic(std::move(used), std::move(*certificate));
    }

    bool definitional(const k::Proposition& goal) const {
        const auto* equality = std::get_if<k::Eq>(&goal.node);
        if (equality == nullptr)
            return false;
        const auto lhs = k::normalize(context_, equality->lhs, k::CoreLimits{});
        const auto rhs = k::normalize(context_, equality->rhs, k::CoreLimits{});
        return lhs && rhs && *lhs == *rhs;
    }

    k::Type type_of_variable(std::uint32_t index) const {
        return binders_[binders_.size() - 1 - index];
    }

    // Rewrites with the premises' equalities, and with equalities between
    // variables that arithmetic establishes, until nothing applies; then closes
    // what is left definitionally or by arithmetic.
    std::optional<k::ProofTerm> rewrite_then_close(const k::Proposition& goal) {
        std::vector<Equality> known;
        for (const auto& fact : facts()) {
            const auto& equality = std::get<k::Eq>(fact.proposition.node);
            if (equality.type == k::Type{k::kBoolean})
                continue;
            known.push_back(Equality{equality.type, equality.lhs, equality.rhs, *fact.evidence});
        }

        std::vector<Rewrite> steps;
        k::Proposition current = goal;
        std::size_t derivations = 0;
        for (int round = 0; round < 4; ++round) {
            const bool changed = rewrite(current, known, steps);
            std::vector<Equality> derived = derive(current, known, derivations);
            if (derived.empty() && !changed)
                break;
            for (auto& equality : derived)
                known.push_back(std::move(equality));
        }

        std::optional<k::ProofTerm> proof;
        if (definitional(current)) {
            proof = k::ProofTerm::reflexivity();
        } else {
            proof = by_arithmetic(current);
        }
        if (!proof)
            return std::nullopt;
        for (auto& step : std::views::reverse(steps)) {
            proof = k::ProofTerm::equality_elimination(step.equality.type, step.equality.lhs, step.equality.rhs,
                                                       step.motive, step.equality.evidence, std::move(*proof));
        }
        return proof;
    }

    // Rewrites left to right wherever a left side occurs, never with an
    // equality whose right side contains its left side.
    static bool rewrite(k::Proposition& current, const std::vector<Equality>& known, std::vector<Rewrite>& steps) {
        bool changed = false;
        for (std::size_t budget = 0; budget < kMaxRewrites; ++budget) {
            bool applied = false;
            for (const auto& equality : std::views::reverse(known)) {
                if (equality.lhs == equality.rhs || occurs(equality.rhs, equality.lhs))
                    continue;
                auto motive = obligations::rewrite_context(current, equality.lhs);
                if (!motive)
                    continue;
                current = k::instantiate(*motive, equality.rhs);
                steps.push_back(Rewrite{equality, std::move(*motive)});
                applied = true;
                break;
            }
            if (!applied)
                break;
            changed = true;
        }
        return changed;
    }

    // Equalities between a variable of the goal and another variable of the
    // same type, where arithmetic over the premises proves them.
    std::vector<Equality> derive(const k::Proposition& current, const std::vector<Equality>& known,
                                 std::size_t& derivations) const {
        std::vector<Equality> derived;
        const auto* equality = std::get_if<k::Eq>(&current.node);
        if (equality == nullptr)
            return derived;
        std::set<std::uint32_t> mentioned;
        variables_of(equality->lhs, mentioned);
        variables_of(equality->rhs, mentioned);
        for (const std::uint32_t variable : mentioned) {
            if (variable >= binders_.size())
                continue;
            for (std::uint32_t other = 0; other < binders_.size(); ++other) {
                if (other == variable || !(type_of_variable(other) == type_of_variable(variable))) {
                    continue;
                }
                const auto lhs = k::Term::variable(k::VarIndex{variable});
                const auto rhs = k::Term::variable(k::VarIndex{other});
                const bool already = std::ranges::any_of(known, [&](const Equality& e) {
                    return (e.lhs == lhs && e.rhs == rhs) || (e.lhs == rhs && e.rhs == lhs);
                });
                if (already || ++derivations > kMaxDerivations)
                    continue;
                const k::Type type = type_of_variable(variable);
                const auto proposition = k::Proposition::equality(type, lhs, rhs);
                if (auto proof = by_arithmetic(proposition)) {
                    derived.push_back(Equality{type, lhs, rhs, std::move(*proof)});
                }
            }
        }
        return derived;
    }

    const k::Context& context_;
    bool rewriting_;
    std::vector<k::Type> binders_;
    std::vector<Premise> premises_;
};

} // namespace

std::optional<kernel::ArithmeticCertificate> refute(const kernel::ArithmeticSystem& system) {
    Search search(system);
    return search.solve();
}

std::optional<kernel::ProofTerm> arithmetic_evidence(const kernel::Context& context, const kernel::Proposition& goal,
                                                     bool rewriting) {
    Prover prover(context, rewriting);
    return prover.prove(goal);
}

} // namespace cppl::automation
