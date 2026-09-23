#pragma once

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/obligations/contracts.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/ids.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cppl::obligations {

// A content-derived identity: the same obligation, produced by any run of the
// same compiler on the same input, has the same id (ARCHITECTURE.md 53).
struct ObligationId {
    source::Digest digest;

    [[nodiscard]] std::string text() const {
        return digest.to_short_hex(16);
    }

    friend bool operator==(const ObligationId&, const ObligationId&) = default;
};

enum class Origin : std::uint8_t {
    LawProposition,
    ProofProposition,
    FunctionContract,
    CallPrecondition,
    ReturnPath,
    LoopEntry,              // a loop invariant holds when the loop is entered
    LoopPreservation,       // an iteration re-establishes a loop invariant
    LoopDescent,            // an iteration strictly decreases a loop measure (SPEC.md 24.3)
    RefinementIntroduction, // a value enters a refinement type (SPEC.md 17.2)
    // A subscript's index is within its array's extent (SPEC.md 12.10
    // VERIFIED-038). This is a proposition about values, so the kernel proves
    // it; only the capability part of an access is tracked contextually.
    ElementBounds,
    // A case omitted from a `cases` statement cannot occur (SPEC.md 20.2
    // CASE-004). Distinct from ImpossiblePath even though the same checked
    // contradiction discharges both: they claim different things, and CASE-012
    // and CASE-016 forbid reporting one as the other.
    OmittedCase,
    // A runtime path is unreachable (SPEC.md 12.7 VERIFIED-023).
    ImpossiblePath
};

std::string describe(Origin origin);

// The identity of a claim that a context cannot occur: an omitted case, or an
// unreachable runtime path (SPEC.md CASE-016). The origin is part of it, so the
// two never share an identity even when they state the same proposition and are
// discharged by the same evidence. `position` separates two claims with the
// same subject and goal by the order they were written in, which, unlike a line
// number, survives unrelated edits.
[[nodiscard]] ObligationId identify_impossibility(Origin origin, const kernel::Context& context,
                                                  const std::string& subject, const kernel::Proposition& goal,
                                                  std::uint64_t position);

struct Obligation {
    ObligationId id;
    Origin origin = Origin::LawProposition;

    // What the obligation is about: a Law's name, or a verified function's
    // qualified name.
    std::string subject;

    // The Law this obligation states, where it states one. A contract states
    // no Law, so no written proof can name it: it is discharged from the
    // function's own body or not at all.
    std::optional<vir::LawId> law;
    std::optional<vir::ProofId> proof;

    // An explicit assumption the author wrote `trusted` for (SPEC.md 27).
    // Nothing discharges it: it is reported as TRUSTED, never as proven, and
    // the trust report names it.
    bool trusted = false;

    kernel::Proposition goal;
    source::SourceRange range;

    // Evidence built while lowering what the author wrote, for a claim that is
    // made inside a proof but stands as an obligation of its own: an omitted
    // case. It is submitted to the kernel against `goal` like any other
    // evidence and believed no more than any other.
    std::optional<kernel::ProofTerm> evidence;
};

// Evidence an author wrote, lowered to a kernel proof term.
//
// Lowering decides what the written statement means as a proof term. It does
// not decide whether that term proves anything: the term goes to the kernel
// like any other (TRUST.md 5.1).
struct WrittenProof {
    vir::ProofId id;
    std::string name;
    std::optional<vir::LawId> law;

    // The proposition the `proves` clause claims: the law named there,
    // instantiated at the arguments it was named with, closed over the proof's
    // own parameters. It is the law's own proposition exactly when the proof
    // discharges the law rather than one instance of it.
    kernel::Proposition goal;
    bool closes_law = false;

    kernel::ProofTerm term;
    source::SourceRange range;
};

// Everything the kernel needs in order to decide the obligations of one
// translation unit: the definitions it may unfold, the goals themselves, and
// the evidence authors wrote for them.
// A refinement type's predicate, ready to be stated of a value (SPEC.md 17).
//
// `parameters` are the declaration's indices and then the value being refined,
// in that order, so the predicate is specialized at index values and the value
// exactly as a callee's precondition is specialized at a call's arguments.
struct RefinementPredicate {
    std::string name;
    std::vector<kernel::Type> parameters;
    kernel::Proposition predicate;
    std::string identity = {};
};

struct Program {
    kernel::Context context;
    std::vector<Obligation> obligations;
    std::vector<WrittenProof> proofs;
    std::vector<ContractVerification> contracts;
    std::vector<RefinementPredicate> refinements;

    // The predicate a refinement name states, or null when nothing declares it.
    [[nodiscard]] const RefinementPredicate* refinement(std::string_view name) const;

    // Laws whose written proof was refused. Their obligation stays open: the
    // author said how the law is established, and that evidence did not hold.
    std::vector<vir::LawId> refused_proofs;

    [[nodiscard]] const WrittenProof* proof_for(vir::LawId law) const;
    [[nodiscard]] bool proof_refused(vir::LawId law) const;

    // The same, for an obligation that may state no Law at all.
    [[nodiscard]] const WrittenProof* proof_for(const Obligation& obligation) const;
    [[nodiscard]] bool proof_refused(const Obligation& obligation) const;
};

// Written refl introduces binders but never uses the hypotheses it introduces.
//
// A disjunctive goal has two shapes of evidence and nothing here decides which
// side holds, so `prefer_right` selects the one this candidate introduces. The
// caller offers both and the kernel decides; neither is believed.
[[nodiscard]] kernel::ProofTerm definitional_evidence(const kernel::Proposition& goal, bool prefer_right = false);

[[nodiscard]] kernel::ProofTerm automatic_evidence(const kernel::Proposition& goal, bool prefer_right = false);

[[nodiscard]] std::optional<kernel::Proposition> rewrite_context(const kernel::Proposition& goal,
                                                                 const kernel::Term& target);

} // namespace cppl::obligations
