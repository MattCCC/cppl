#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/ids.hpp"

namespace cppl::obligations {

// A content-derived identity: the same obligation, produced by any run of the
// same compiler on the same input, has the same id (ARCHITECTURE.md 32).
struct ObligationId {
    source::Digest digest;

    [[nodiscard]] std::string text() const { return digest.to_short_hex(16); }

    friend bool operator==(const ObligationId&, const ObligationId&) = default;
};

enum class Origin : std::uint8_t {
    LawProposition,
};

std::string describe(Origin origin);

struct Obligation {
    ObligationId id;
    vir::LawId law;
    std::string law_name;
    Origin origin = Origin::LawProposition;
    kernel::Proposition goal;
    source::SourceRange range;
};

// Which written proof statement produced a piece of evidence.
//
// Kept as a typed fact rather than a tactic name, so a diagnostic can say what
// the author asked for without any component re-reading the source.
enum class WrittenProofKind : std::uint8_t {
    Reflexivity,
    Exact,
    Apply,
};

std::string describe(WrittenProofKind kind);

// Evidence an author wrote, lowered to a kernel proof term.
//
// Lowering decides what the written statement means as a proof term. It does
// not decide whether that term proves anything: the term goes to the kernel
// like any other (TRUST.md 41).
struct WrittenProof {
    vir::ProofId id;
    std::string name;
    vir::LawId law;
    WrittenProofKind kind = WrittenProofKind::Reflexivity;
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

    // Laws whose written proof was refused. Their obligation stays open: the
    // author said how the law is established, and that evidence did not hold.
    std::vector<vir::LawId> refused_proofs;

    [[nodiscard]] const WrittenProof* proof_for(vir::LawId law) const;
    [[nodiscard]] bool proof_refused(vir::LawId law) const;
};

// Evidence with the shape of a goal: introduce every quantifier, then offer
// reflexivity. Whether the two sides of the equality really are definitionally
// equal is the kernel's decision, made by its own normalizer.
[[nodiscard]] kernel::ProofTerm definitional_evidence(const kernel::Proposition& goal);

}  // namespace cppl::obligations
