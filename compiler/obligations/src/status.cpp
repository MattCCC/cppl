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

const WrittenProof* Program::proof_for(vir::LawId law) const {
    for (const WrittenProof& proof : proofs) {
        if (proof.law == law && proof.closes_law) {
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
    // A precondition is supposed and then left alone. This evidence establishes
    // the conclusion outright, which is a claim about the conclusion and never
    // an appeal to the premise: the hypothesis it introduces is not used.
    if (const auto* implication = std::get_if<kernel::Implies>(&goal.node)) {
        return kernel::ProofTerm::implication_introduction(
            *implication->premise, definitional_evidence(*implication->conclusion));
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
