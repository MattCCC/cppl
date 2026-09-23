#pragma once

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/status.hpp"

#include <optional>
#include <string>
#include <vector>

namespace cppl::automation {

// Candidate evidence for a goal, together with the strategy that produced it.
//
// This layer proposes; it does not decide. Whatever it returns is handed to the
// kernel, and the kernel's answer is the result (AGENTS.md 19, TRUST.md 6).
struct Evidence {
    kernel::ProofTerm proof;
    std::string strategy;
};

[[nodiscard]] std::optional<Evidence> propose(const kernel::Context& context, const kernel::Proposition& goal);

// Submits every obligation to the kernel and records what came back.
//
// `transitions`, when given, receives the number of search steps composing the
// evidence took. The count is deterministic: the same program always costs the
// same, on any machine. A search that cannot make progress stops rather than
// continuing, so composition terminates on every input and never reads evidence
// that was not established.
[[nodiscard]] std::vector<obligations::ObligationResult> verify(const obligations::Program& program,
                                                                diagnostics::Engine& engine,
                                                                std::size_t* transitions = nullptr);

} // namespace cppl::automation
