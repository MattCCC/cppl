#pragma once

#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/status.hpp"
#include "cppl/source/location.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cppl::obligations {

// What a proven claim is. Each is reported under its own name: an omitted case
// and an unreachable runtime path are never counted as each other, nor as the
// proof they are written in (SPEC.md CASE-012, CASE-016).
enum class ClaimKind : std::uint8_t {
    Law,
    Proof,         // a proof declaration with a proposition of its own
    LawInstance,   // a proof of one instance of a law, which has no obligation of its own
    Contract,      // a verified function's contract
    OmittedCase,   // SPEC.md CASE-004
    ImpossiblePath // SPEC.md VERIFIED-023
};

std::string describe(ClaimKind kind);

// An unsafe block a proven contract rests on (SPEC.md 26, TRUST.md
// TCB-REPORT-005). The contract was proven with the block's effects modeled as
// unknown writes, so it holds only if the block's own code is sound, which
// nothing checked.
struct UnsafeDependency {
    source::SourceLocation location;
    // Whether the block is in the contract's own body, rather than in the body
    // of a verified function it calls.
    bool direct = false;
};

// A proven claim and the trusted laws it rests on: its trust closure (TRUST.md
// 2.8, 35).
struct ClaimClosure {
    ClaimKind kind = ClaimKind::Law;
    std::string subject;
    source::SourceLocation location;

    // The content identity of what the claim states: its obligation's, a
    // partial contract's conditions', or for a proof of a law instance, the
    // goal it proves relative to its premises (TRUST.md 36.1).
    ObligationId identity;

    // Ordered by law. `direct` is set where the claim's own evidence names the
    // law; otherwise it arrives through a proof the claim uses or a verified
    // function it calls. Empty for a claim proven outright.
    std::vector<TrustedPremise> premises;

    // For a contract, the unsafe blocks it rests on, its own and those of every
    // verified function it calls, ordered by location. A claim that a path or
    // a case of a verified body cannot occur rests on those of that body's
    // contract. Only these claims have runtime code to rest on.
    std::vector<UnsafeDependency> unsafe = {};
};

// The trust closure of every proven claim of one translation unit.
//
// A proof's closure is the set of premises the kernel checked it relative to,
// which already includes those of every proof it uses. A contract's closure is
// that of every obligation of its body, joined with the closures of the
// contracts it calls, computed to a fixed point so a recursive call graph is
// closed like any other.
struct TrustClosure {
    // Every proven claim, laws and proofs in the order the program states
    // them, then proofs of law instances, then contracts.
    std::vector<ClaimClosure> claims;

    // Every trusted law the unit declares, ordered by law.
    std::vector<TrustedPremise> assumptions;

    // Every trusted law admitting a memory proposition, in declaration order.
    // No claim can rest on one (SPEC.md TRUSTED-003, TRUSTED-008), so each is
    // also unused; it is listed so that the assumption stays visible.
    std::vector<TrustedMemoryAssumption> memory_assumptions;

    // Dependencies that could not be attributed to a claim. Any one means the
    // report cannot vouch for the closures above, so it is an internal error
    // rather than an omission (TRUST.md 2.10, TCB-REPORT-006).
    std::vector<std::string> faults;
};

[[nodiscard]] TrustClosure close_trust(const Program& program, const std::vector<ObligationResult>& results);

} // namespace cppl::obligations
