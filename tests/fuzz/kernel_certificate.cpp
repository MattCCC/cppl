// The kernel's certificate checker, on integer systems and certificates
// decoded directly from bytes (docs/KERNEL.md, "Certificate checking"; TRUST.md
// TCB-CORE-006, TCB-CORE-009).
//
// kernel_arithmetic reaches the checker through systems the kernel states for
// real facts. This reaches it without that translation, so every certificate
// shape, index and multiplier can be tried against every system shape: a
// Farkas sum naming constraints out of order or out of range, a split with a
// zero or repeated coefficient, a case on a disjunction that is not there,
// multipliers near the overflow edge.
//
// Property: a certificate the checker accepts refutes the system, so no integer
// point satisfies every constraint and one member of every disjunction. The
// target searches a box of points for one; finding one is a soundness finding.
// Its absence proves nothing, and a rejection is never a finding. The
// untrusted refutation search's own certificate is put to the same test.

#include "cppl/kernel/box.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/linear.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/refutation/refute.hpp"
#include "cppl/testing/fuzz.hpp"
#include "cppl/testing/kernel_generator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace k = cppl::kernel;
namespace gen = cppl::testing::kernel_generator;
using Wide = k::Wide;

constexpr Wide kBox = 5;

Wide coefficient(gen::Choices& choices) {
    // Mostly small, sometimes at the edge of what the checker multiplies.
    switch (choices.below(8)) {
        case 0:
            return (Wide{1} << 62) * (choices.below(2) == 0 ? 1 : -1);
        case 1:
            return Wide{0};
        default:
            return Wide{choices.below(9)} - 4;
    }
}

k::LinearConstraint constraint(gen::Choices& choices, std::uint32_t variables) {
    k::LinearConstraint result;
    for (std::uint32_t variable = 0; variable < variables; ++variable) {
        const Wide value = coefficient(choices);
        if (value != 0) {
            result.terms.emplace_back(variable, value);
        }
    }
    result.constant = Wide{choices.below(15)} - 7;
    return result;
}

k::ArithmeticCertificate certificate(gen::Choices& choices, std::uint32_t variables, std::size_t constraints,
                                     std::size_t disjunctions, unsigned depth) {
    const std::uint32_t shape = depth == 0 ? 0 : choices.below(4);
    if (shape == 1) {
        std::vector<std::pair<std::uint32_t, std::int64_t>> terms;
        const std::uint32_t count = choices.below(variables + 2);
        for (std::uint32_t index = 0; index < count; ++index) {
            terms.emplace_back(choices.below(variables + 1), static_cast<std::int64_t>(choices.below(7)) - 3);
        }
        const auto constant = static_cast<std::int64_t>(choices.below(9)) - 4;
        auto low = certificate(choices, variables, constraints + 1, disjunctions, depth - 1);
        auto high = certificate(choices, variables, constraints + 1, disjunctions, depth - 1);
        return k::ArithmeticCertificate{k::IntegerSplit{std::move(terms), constant,
                                                        k::Box<k::ArithmeticCertificate>{std::move(low)},
                                                        k::Box<k::ArithmeticCertificate>{std::move(high)}}};
    }
    if (shape == 2) {
        const std::uint32_t which = choices.below(static_cast<std::uint32_t>(disjunctions + 1));
        auto first = certificate(choices, variables, constraints + 1, disjunctions, depth - 1);
        auto second = certificate(choices, variables, constraints + 1, disjunctions, depth - 1);
        return k::ArithmeticCertificate{k::DisjunctionCases{which, k::Box<k::ArithmeticCertificate>{std::move(first)},
                                                            k::Box<k::ArithmeticCertificate>{std::move(second)}}};
    }
    k::FarkasSum sum;
    const std::uint32_t count = choices.below(static_cast<std::uint32_t>(constraints + 2));
    std::uint32_t position = choices.below(2);
    for (std::uint32_t index = 0; index < count; ++index) {
        const Wide multiplier = choices.below(10) == 0 ? (Wide{1} << 61) : Wide{choices.below(5)};
        sum.multipliers.emplace_back(position, multiplier);
        // Usually increasing, as the checker requires; sometimes not.
        position = choices.below(8) == 0 ? position : position + 1 + choices.below(2);
    }
    return k::ArithmeticCertificate{std::move(sum)};
}

bool holds(const k::LinearConstraint& constraint, const std::vector<Wide>& point) {
    Wide sum = constraint.constant;
    for (const auto& [variable, value] : constraint.terms) {
        Wide product = 0;
        if (__builtin_mul_overflow(value, point[variable], &product) || __builtin_add_overflow(sum, product, &sum)) {
            return false;
        }
    }
    return sum <= 0;
}

// A point in the box satisfying the system, if the search finds one.
std::optional<std::vector<Wide>> solution(const k::ArithmeticSystem& system) {
    const std::size_t variables = system.variables.size();
    std::vector<Wide> point(variables, -kBox);
    while (true) {
        bool satisfied = true;
        for (const auto& constraint : system.constraints) {
            satisfied = satisfied && holds(constraint, point);
        }
        for (const auto& members : system.disjunctions) {
            satisfied = satisfied && (holds(members[0], point) || holds(members[1], point));
        }
        if (satisfied) {
            return point;
        }
        std::size_t position = 0;
        while (position < variables && point[position] == kBox) {
            point[position] = -kBox;
            ++position;
        }
        if (position == variables) {
            return std::nullopt;
        }
        ++point[position];
    }
}

std::string show(const std::vector<Wide>& point) {
    std::string text;
    for (const Wide value : point) {
        text += k::describe(value) + " ";
    }
    return text;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    gen::ByteChoices choices({data, size});
    k::ArithmeticSystem system;
    const std::uint32_t variables = 1 + choices.below(3);
    for (std::uint32_t variable = 0; variable < variables; ++variable) {
        system.variables.push_back(
            k::ArithmeticVariable{k::VariableRole::Value, k::kBoolean, k::Term::literal(k::kBoolean, 0), 0, 0});
    }
    const std::uint32_t constraints = choices.below(7);
    for (std::uint32_t index = 0; index < constraints; ++index) {
        system.constraints.push_back(constraint(choices, variables));
    }
    const std::uint32_t disjunctions = choices.below(3);
    for (std::uint32_t index = 0; index < disjunctions; ++index) {
        system.disjunctions.push_back({constraint(choices, variables), constraint(choices, variables)});
    }

    k::CoreLimits limits;
    limits.max_certificate_nodes = 64;
    const std::optional<std::vector<Wide>> witness = solution(system);

    const k::ArithmeticCertificate decoded =
        certificate(choices, variables, system.constraints.size(), system.disjunctions.size(), 3);
    const auto first = k::refutes(system, decoded, limits);
    const auto second = k::refutes(system, decoded, limits);
    cppl::testing::fuzz::require(first.has_value() == second.has_value(), "the checker's verdict is deterministic");
    if (first && witness) {
        cppl::testing::fuzz::require_none("SOUNDNESS: a decoded certificate refutes a system satisfied at " +
                                          show(*witness));
    }
    if (!first) {
        cppl::testing::fuzz::require(!first.error().empty(), "a refusal says why");
    }

    if (const auto proposed = cppl::refutation::refute(system)) {
        if (k::refutes(system, *proposed, k::CoreLimits{}) && witness) {
            cppl::testing::fuzz::require_none("SOUNDNESS: a proposed certificate refutes a system satisfied at " +
                                              show(*witness));
        }
    }
    return 0;
}
