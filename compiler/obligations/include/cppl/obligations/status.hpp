#pragma once

#include "cppl/kernel/check.hpp"
#include "cppl/obligations/obligation.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cppl::obligations {

// The verification statuses of SPEC.md 38. They are distinct states and are
// never collapsed into one generic "verified" result.
enum class Status : std::uint8_t {
    Proven,
    Trusted,
    RuntimeChecked,
    Unsafe,
    Unverified,
    Unresolved,
};

std::string describe(Status status);

// The outcome of one obligation.
//
// `Proven` cannot be constructed without a kernel Acceptance, and the
// acceptance must carry exactly the proposition of the obligation being
// discharged. There is no other path to this status: no frontend, elaborator,
// solver adapter, diagnostic or test can produce one (AGENTS.md 5).
//
// Evidence that rests on trusted laws is accepted for the obligation's goal
// supposed under them, and the verdict keeps them: the claim is PROVEN relative
// to those premises and to nothing else (SPEC.md STATUS-002, TRUSTED-002). An
// acceptance for the goal under any other premises establishes nothing.
class Verdict {
  public:
    [[nodiscard]] static Verdict proven(const kernel::Acceptance& acceptance, const Obligation& obligation,
                                        std::vector<TrustedPremise> premises = {});

    // An assumption the author stated explicitly (SPEC.md 27). This is not a
    // weaker kind of proof: nothing was proved, and the trust report names every
    // one of these. It exists so that a gap C++L cannot close is recorded where
    // it can be audited, instead of being closed silently.
    [[nodiscard]] static Verdict trusted(std::string declaration);

    [[nodiscard]] static Verdict unresolved(std::string reason);

    [[nodiscard]] Status status() const noexcept {
        return status_;
    }
    [[nodiscard]] const std::string& reason() const noexcept {
        return reason_;
    }
    [[nodiscard]] bool is_proven() const noexcept {
        return status_ == Status::Proven;
    }
    [[nodiscard]] bool is_trusted() const noexcept {
        return status_ == Status::Trusted;
    }

    // The trusted laws a proven claim rests on. Empty for any other status, and
    // for a claim proven outright.
    [[nodiscard]] const std::vector<TrustedPremise>& premises() const noexcept {
        return premises_;
    }

  private:
    Verdict(Status status, std::string reason, std::vector<TrustedPremise> premises = {})
        : status_(status),
          reason_(std::move(reason)),
          premises_(std::move(premises)) {}

    Status status_ = Status::Unresolved;
    std::string reason_;
    std::vector<TrustedPremise> premises_;
};

struct ObligationResult {
    Obligation obligation;
    Verdict verdict;
    std::string strategy; // what produced the evidence, for reporting only
};

} // namespace cppl::obligations
