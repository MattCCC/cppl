#pragma once

#include "cppl/artifact/interface.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/status.hpp"
#include "cppl/source/digest.hpp"
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

// A contract another translation unit proved that a claim rests on, and what
// that unit's proof of it rests on in turn (SPEC.md TUBOUND-006). The claim holds
// only if the verification interface that recorded the contract is faithful to
// a proof that was really made, which nothing in this unit checked, so a claim
// that rests on one is never reported as assumption-free (TRUST.md 31).
struct ImportedDependency {
    std::string name;   // the callee's qualified name
    std::string symbol; // its USR
    std::string origin; // the interface that recorded it
    source::Digest entry;
    bool total = false;
    std::vector<artifact::Premise> premises;
    std::vector<artifact::UnsafeBlock> unsafe;
    std::vector<artifact::Dependency> depends;
    // Whether the claim's own body calls it, rather than a verified function
    // of this unit that the body calls.
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

    // For a contract, whether it is a total-correctness claim rather than one
    // that holds only if the function returns (SPEC.md CORRECT-006).
    bool total = true;

    // For a contract, the contracts of other units it was proven through, its
    // own calls' and those of every verified function of this unit it calls,
    // ordered by symbol. A claim that a path or a case of a verified body
    // cannot occur rests on those of that body's contract (SPEC.md TUBOUND-006).
    std::vector<ImportedDependency> imported = {};

    // For a contract, the function's USR, so an interface can record it.
    std::string symbol = {};
};

// Whether a claim rests on a trusted law, of this unit or of another unit whose
// contract it was proven through (TRUST.md 3.2, TCB-REPORT-002).
[[nodiscard]] bool rests_on_trusted_laws(const ClaimClosure& claim);

// Whether a claim rests on unsafe code, of this unit or of another unit whose
// contract it was proven through (TRUST.md TCB-REPORT-005).
[[nodiscard]] bool rests_on_unsafe_code(const ClaimClosure& claim);

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

    // Every contract of another unit this unit established from an interface,
    // in contract order, whether or not a proven claim rests on it, so the
    // report says what was assumed from elsewhere (SPEC.md TUBOUND-006).
    std::vector<ImportedDependency> imports;

    // Dependencies that could not be attributed to a claim. Any one means the
    // report cannot vouch for the closures above, so it is an internal error
    // rather than an omission (TRUST.md 2.10, TCB-REPORT-006).
    std::vector<std::string> faults;
};

[[nodiscard]] TrustClosure close_trust(const Program& program, const std::vector<ObligationResult>& results);

} // namespace cppl::obligations
