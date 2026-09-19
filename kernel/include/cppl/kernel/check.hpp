#pragma once

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <utility>

namespace cppl::kernel {

enum class RejectionKind : std::uint8_t {
    MalformedProposition,   // the goal itself is not a well-formed proposition
    MalformedProofTerm,     // the evidence is structurally invalid
    ProofShapeMismatch,     // the rule applied does not introduce this goal
    NotDefinitionallyEqual, // reflexivity was offered for terms that differ
    CoreFailure,            // typing, depth or budget failure while checking
};

std::string describe(RejectionKind kind);

struct Rejection {
    RejectionKind kind = RejectionKind::MalformedProofTerm;
    std::string detail;
};

class Acceptance;
class Context;

// The single entry point that can establish a proposition.
[[nodiscard]] std::expected<Acceptance, Rejection> check(const Context& context, const Proposition& proposition,
                                                         const ProofTerm& proof, const CoreLimits& limits);

// Evidence that the kernel accepted a specific proposition.
//
// Acceptance cannot be constructed anywhere else: check() is its only friend.
// Every layer above the kernel that reports PROVEN must hold one of these and
// must compare the proposition it carries against the obligation it claims to
// have discharged, so an acceptance obtained for one goal cannot be presented
// for another (TRUST.md, AGENTS.md 5).
class Acceptance {
  public:
    [[nodiscard]] const Proposition& proposition() const noexcept {
        return proposition_;
    }

  private:
    explicit Acceptance(Proposition proposition) : proposition_(std::move(proposition)) {}

    friend std::expected<Acceptance, Rejection> check(const Context&, const Proposition&, const ProofTerm&,
                                                      const CoreLimits&);

    Proposition proposition_;
};

using CheckResult = std::expected<Acceptance, Rejection>;

} // namespace cppl::kernel
