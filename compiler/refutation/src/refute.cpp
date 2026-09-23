#include "cppl/refutation/refute.hpp"

#include "cppl/kernel/types.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace cppl::refutation {

namespace {

namespace k = kernel;
using Wide = k::Wide;

constexpr std::size_t kMaxRows = 4096;
constexpr std::size_t kMaxSearchNodes = 512;
constexpr Wide kMaxPinnedRange = 16;

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

} // namespace

std::optional<kernel::ArithmeticCertificate> refute(const kernel::ArithmeticSystem& system) {
    Search search(system);
    return search.solve();
}

} // namespace cppl::refutation
