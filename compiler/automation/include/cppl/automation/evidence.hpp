#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/status.hpp"

namespace cppl::automation {

// Candidate evidence for a goal, together with the strategy that produced it.
//
// This layer proposes; it does not decide. Whatever it returns is handed to the
// kernel, and the kernel's answer is the result (AGENTS.md 19, TRUST.md 3).
struct Evidence {
    kernel::ProofTerm proof;
    std::string strategy;
};

[[nodiscard]] std::optional<Evidence> propose(const kernel::Context& context,
                                              const kernel::Proposition& goal);

// Submits every obligation to the kernel and records what came back.
[[nodiscard]] std::vector<obligations::ObligationResult> verify(
    const obligations::Program& program,
    diagnostics::Engine& engine);

}  // namespace cppl::automation
