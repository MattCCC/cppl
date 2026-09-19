#pragma once

#include "cppl/kernel/check.hpp"
#include "cppl/obligations/obligation.hpp"

#include <cstdint>
#include <string>
#include <utility>

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
class Verdict {
  public:
    [[nodiscard]] static Verdict proven(const kernel::Acceptance& acceptance, const Obligation& obligation);

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

  private:
    Verdict(Status status, std::string reason) : status_(status), reason_(std::move(reason)) {}

    Status status_ = Status::Unresolved;
    std::string reason_;
};

struct ObligationResult {
    Obligation obligation;
    Verdict verdict;
    std::string strategy; // what produced the evidence, for reporting only
};

} // namespace cppl::obligations
