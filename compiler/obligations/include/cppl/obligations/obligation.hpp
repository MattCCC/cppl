#pragma once

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/obligations/contracts.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/ids.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
    // A runtime path is unreachable: `contradiction evidence;` written in a
    // verified body (SPEC.md 12.7 VERIFIED-023, VERIFIED-045).
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

// A trusted law that evidence rests on (SPEC.md TRUSTED-002, FOUNDATIONS.md
// 130). Evidence established relative to trusted laws is checked by the kernel
// with each of their propositions supposed as a premise, so the list it carries
// is exactly what it may use without proof, and nothing it uses can be missing
// from it.
struct TrustedPremise {
    vir::LawId law;
    std::string name;
    // The trusted law's own obligation, whose identity is derived from its
    // proposition rather than its spelling (TRUST.md TCB-TRUST-005).
    ObligationId identity;
    source::SourceLocation location;
    kernel::Proposition proposition;
    // Whether the evidence names the law itself, rather than using a proof
    // that does. Reporting only: the kernel sees the same premise either way.
    bool direct = false;
};

// What evidence established relative to `premises` proves: `goal`, supposed
// under each premise in turn, the first outermost. With no premises it is the
// goal itself.
[[nodiscard]] kernel::Proposition relative_to(const std::vector<TrustedPremise>& premises, kernel::Proposition goal);

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

    // Evidence built from what the author wrote, for a claim that stands as an
    // obligation of its own: an omitted case, or a runtime path claimed not to
    // occur. It is submitted to the kernel against `goal` like any other
    // evidence and believed no more than any other.
    std::optional<kernel::ProofTerm> evidence;

    // The trusted laws that evidence rests on: it proves
    // `relative_to(assumptions, goal)`, which is what the kernel is given.
    std::vector<TrustedPremise> assumptions;

    // Why the evidence written for such a claim could not be built. The reason
    // was reported where it was found, so the claim stands unproven and is
    // never offered to a strategy that might establish it some other way
    // (SPEC.md CASE-005, CASE-015).
    std::optional<std::string> refusal;
};

// A claim that a runtime path cannot occur, `contradiction evidence;` written
// in a verified body (SPEC.md VERIFIED-023), awaiting the evidence it names.
//
// Its obligation is generated with the body's other conditions, as the path's
// facts closed over `False`. The evidence can be built only once the proof the
// claim names has been, so the claim is recorded here and discharged after the
// written proofs are lowered.
struct PathClaim {
    std::size_t obligation = 0; // into Program::obligations
    std::optional<vir::ProofId> proof;
    std::string evidence; // the name as written
    // The terms the proof is instantiated at, stated beneath every binder of
    // the obligation's goal, with their types.
    std::vector<kernel::Term> arguments;
    std::vector<kernel::Type> argument_types;
    source::SourceLocation location;
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

    // The trusted laws the proof rests on, its own and those of every proof it
    // uses, ordered by declaration. `term` proves `relative_to(assumptions,
    // goal)`: a proof that names no trusted law, directly or through another
    // proof, has none and proves `goal` itself.
    std::vector<TrustedPremise> assumptions;

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

// A trusted law whose conclusion is a memory proposition (SPEC.md TRUSTED-003).
//
// It is an explicit assumption like any trusted law, TRUSTED and never proven,
// but it states no proposition the kernel checks, so it is carried here rather
// than as an obligation: nothing can suppose it as a premise, and no claim can
// rest on it (TRUSTED-008). Its identity is derived from what it states.
struct TrustedMemoryAssumption {
    std::string name;
    ObligationId identity;
    source::SourceLocation location;
    // What it admits, as written in the report: `readable(p, n)`.
    std::string statement;
};

struct Program {
    kernel::Context context;
    std::vector<Obligation> obligations;
    std::vector<TrustedMemoryAssumption> memory_assumptions;
    std::vector<WrittenProof> proofs;
    std::vector<ContractVerification> contracts;
    std::vector<RefinementPredicate> refinements;

    // Claims that a runtime path cannot occur whose evidence is built once the
    // written proofs have been lowered. Empty after generation completes.
    std::vector<PathClaim> path_claims;

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
