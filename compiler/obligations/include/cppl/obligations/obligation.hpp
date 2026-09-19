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
// same compiler on the same input, has the same id (ARCHITECTURE.md 32).
struct ObligationId {
    source::Digest digest;

    [[nodiscard]] std::string text() const {
        return digest.to_short_hex(16);
    }

    friend bool operator==(const ObligationId&, const ObligationId&) = default;
};

enum class Origin : std::uint8_t {
    LawProposition,
    FunctionContract,
    CallPrecondition,
    ReturnPath,
};

std::string describe(Origin origin);

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

    kernel::Proposition goal;
    source::SourceRange range;
};

// Evidence an author wrote, lowered to a kernel proof term.
//
// Lowering decides what the written statement means as a proof term. It does
// not decide whether that term proves anything: the term goes to the kernel
// like any other (TRUST.md 41).
struct WrittenProof {
    vir::ProofId id;
    std::string name;
    vir::LawId law;

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
struct Program {
    kernel::Context context;
    std::vector<Obligation> obligations;
    std::vector<WrittenProof> proofs;
    std::vector<ContractVerification> contracts;

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
[[nodiscard]] kernel::ProofTerm definitional_evidence(const kernel::Proposition& goal);

[[nodiscard]] kernel::ProofTerm automatic_evidence(const kernel::Proposition& goal);

[[nodiscard]] std::optional<kernel::Proposition> rewrite_context(const kernel::Proposition& goal,
                                                                 const kernel::Term& target);

} // namespace cppl::obligations
