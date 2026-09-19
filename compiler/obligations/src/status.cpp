#include "cppl/obligations/status.hpp"

#include <algorithm>
#include <variant>

namespace cppl::obligations {

std::string describe(Status status) {
    switch (status) {
        case Status::Proven:
            return "PROVEN";
        case Status::Trusted:
            return "TRUSTED";
        case Status::RuntimeChecked:
            return "RUNTIME-CHECKED";
        case Status::Unsafe:
            return "UNSAFE";
        case Status::Unverified:
            return "UNVERIFIED";
        case Status::Unresolved:
            return "UNRESOLVED";
    }
    return "UNRESOLVED";
}

std::string describe(Origin origin) {
    switch (origin) {
        case Origin::LawProposition:
            return "law proposition";
    }
    return "obligation";
}

std::string describe(WrittenProofKind kind) {
    switch (kind) {
        case WrittenProofKind::Reflexivity:
            return "refl";
        case WrittenProofKind::Exact:
            return "exact";
        case WrittenProofKind::Apply:
            return "apply";
    }
    return "unknown";
}

const WrittenProof* Program::proof_for(vir::LawId law) const {
    for (const WrittenProof& proof : proofs) {
        if (proof.law == law) {
            return &proof;
        }
    }
    return nullptr;
}

bool Program::proof_refused(vir::LawId law) const {
    return std::ranges::find(refused_proofs, law) != refused_proofs.end();
}

kernel::ProofTerm definitional_evidence(const kernel::Proposition& goal) {
    if (const auto* quantified = std::get_if<kernel::Forall>(&goal.node)) {
        return kernel::ProofTerm::forall_introduction(quantified->binder,
                                                      definitional_evidence(*quantified->body));
    }
    return kernel::ProofTerm::reflexivity();
}

Verdict Verdict::proven(const kernel::Acceptance& acceptance, const Obligation& obligation) {
    // The acceptance must be for this obligation's goal. Holding an acceptance
    // for some other proposition establishes nothing about this one.
    if (!(acceptance.proposition() == obligation.goal)) {
        return Verdict(Status::Unresolved,
                       "the kernel accepted a different proposition than this obligation states");
    }
    return Verdict(Status::Proven, {});
}

Verdict Verdict::unresolved(std::string reason) {
    return Verdict(Status::Unresolved, std::move(reason));
}

}  // namespace cppl::obligations
