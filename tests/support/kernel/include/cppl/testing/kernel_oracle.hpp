#pragma once

// The properties every kernel input must keep, checked against kernel_model
// (docs/KERNEL.md, "Testing"). Shared by the in-suite property tests and the
// persistent fuzz targets, so a finding in one is a regression in both.

#include "cppl/kernel/proof.hpp"
#include "cppl/testing/kernel_generator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace cppl::testing::kernel_oracle {

// How often each proof former occurs in accepted evidence, by its position in
// kernel::ProofTerm::node.
inline constexpr std::size_t kRuleCount = std::variant_size_v<decltype(kernel::ProofTerm::node)>;

struct Statistics {
    std::array<std::uint64_t, kernel_generator::kModeCount> generated{};
    std::array<std::uint64_t, kernel_generator::kModeCount> accepted{};
    // Accepted, and found true in the model outright.
    std::array<std::uint64_t, kernel_generator::kModeCount> decided{};
    std::array<std::uint64_t, kRuleCount> rules{};
    std::uint64_t terms = 0;
    std::uint64_t term_assignments = 0;
};

// Checks one sample: determinism, that an acceptance carries the goal, and
// that an accepted goal is not false in any of the model's interpretations.
// Returns the property that failed, described, or nothing.
[[nodiscard]] std::optional<std::string> examine(const kernel_generator::Sample& sample, Statistics& statistics);

// Checks the term operations the kernel's rules rest on, on terms drawn from
// `choices`: normalization keeps a term's type and value and is idempotent,
// and substitution and shifting mean what evaluation in an environment means.
[[nodiscard]] std::optional<std::string> examine_terms(kernel_generator::Choices& choices, Statistics& statistics);

// The proof formers of kernel::ProofTerm, in its order, for reports.
std::string rule_name(std::size_t index);

} // namespace cppl::testing::kernel_oracle
