#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cppl/source/location.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/place.hpp"

namespace cppl::vir {

// A memory capability: what the current state permits at a place (SPEC.md
// 12.10 VERIFIED-037, RFC 0014 §3).
//
// A capability is deliberately *not* a node of `Expr`. The kernel's proposition
// language is closed and every proposition bottoms out in `Eq` over terms;
// `readable(q)` is a property of the execution state, not a computable function
// of any value the kernel holds. Encoding it as a term would require an
// uninterpreted constant, and adding a proposition former for it would put
// memory semantics inside the trusted kernel where they cannot be checked
// (RFC 0014 §10 options A and B, both rejected).
//
// So a capability is carried as a context hypothesis by the obligation layer
// and never reaches the kernel. Keeping it out of the `Expr` variant is what
// makes that structural rather than a discipline someone must remember: there
// is no path from here to `lower_predicate`.
//
// The value obligations that genuinely need proof -- refinement membership and
// a subscript's `index < extent` -- remain ordinary propositions the kernel
// checks, because both sides of those are terms.

// Why a capability holds. A capability is established by a proven obligation or
// by an explicit recorded trusted boundary, and by nothing else; "needed
// therefore assumed" is not a provenance (AGENTS.md storage invariants).
enum class CapabilityOrigin : std::uint8_t {
    // Derived from modeled C++ semantics: address-of a live object, reference
    // binding, array construction, a live local's own storage (VERIFIED-037).
    Semantics,
    // Stated by a caller's contract and owed by the call site.
    Contract,
    // Admitted by an explicit `trusted law` (VERIFIED-044, SPEC.md 27). The
    // dependency is recorded and reported; it is never silent.
    TrustedLaw,
};

enum class CapabilityKind : std::uint8_t {
    Readable,
    Writable,
};

// `readable(p)` and `readable(p, n)` differ only in extent, so one node carries
// both: `extent` absent abbreviates one object (SPEC.md 12.10).
//
// The capability names a *place*, not a pointer value. That is the model's
// central rule: nothing about a pointer's value, including non-nullness,
// establishes a capability (VERIFIED-037).
struct Capability {
    CapabilityKind kind = CapabilityKind::Readable;
    CapabilityOrigin origin = CapabilityOrigin::Semantics;
    Place place;

    // The element count of a sized form, as a source-level spelling resolved by
    // the bridge. Absent for the one-object abbreviation.
    std::vector<Expr> extent;

    // Where the capability was stated or derived, for the diagnostic that must
    // say which boundary introduced it (RFC 0014 §15).
    source::SourceLocation location;

    // The trusted law this came from, when `origin` is `TrustedLaw`. Trust
    // provenance stays attached to every fact derived from it, so the trust
    // report can name it (AGENTS.md 22).
    std::string trusted_law;

    friend bool operator==(const Capability&, const Capability&) = default;
};

std::string describe(const Capability& capability);
std::string describe(CapabilityKind kind);

} // namespace cppl::vir
