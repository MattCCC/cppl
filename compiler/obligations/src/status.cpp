#include "cppl/obligations/status.hpp"

#include <algorithm>
#include <variant>

#include "cppl/kernel/substitution.hpp"

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
        case Origin::FunctionContract:
            return "function contract";
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

const WrittenProof* Program::proof_for(const Obligation& obligation) const {
    return obligation.law.has_value() ? proof_for(*obligation.law) : nullptr;
}

bool Program::proof_refused(const Obligation& obligation) const {
    return obligation.law.has_value() && proof_refused(*obligation.law);
}

namespace {

// A premise supposed on the way into a goal, with the number of binders that
// stood over it at the time.
struct Supposed {
    kernel::Proposition proposition;
    std::size_t binders = 0;
};

kernel::ProofTerm shaped_evidence(const kernel::Proposition& goal,
                                  std::vector<Supposed>& supposed,
                                  std::size_t binders) {
    if (const auto* quantified = std::get_if<kernel::Forall>(&goal.node)) {
        return kernel::ProofTerm::forall_introduction(
            quantified->binder, shaped_evidence(*quantified->body, supposed, binders + 1));
    }

    if (const auto* implication = std::get_if<kernel::Implies>(&goal.node)) {
        supposed.push_back(Supposed{*implication->premise, binders});
        kernel::ProofTerm body = shaped_evidence(*implication->conclusion, supposed, binders);
        supposed.pop_back();
        return kernel::ProofTerm::implication_introduction(*implication->premise,
                                                           std::move(body));
    }

    // A goal that is exactly a premise supposed on the way in is closed by that
    // premise, innermost first. This is a structural match against what the
    // goal itself introduced, not a search: no equality is used to transform
    // anything, and nothing outside the goal is consulted.
    for (std::size_t position = supposed.size(); position > 0; --position) {
        const Supposed& entry = supposed[position - 1];
        const kernel::Proposition available = kernel::shift(
            entry.proposition, static_cast<std::uint32_t>(binders - entry.binders));
        if (available == goal) {
            return kernel::ProofTerm::hypothesis(
                kernel::HypothesisIndex{static_cast<std::uint32_t>(supposed.size() - position)});
        }
    }

    for (std::size_t position = supposed.size(); position > 0; --position) {
        const Supposed& entry = supposed[position - 1];
        const kernel::Proposition available = kernel::shift(
            entry.proposition, static_cast<std::uint32_t>(binders - entry.binders));
        const auto* equality = std::get_if<kernel::Eq>(&available.node);
        if (equality == nullptr) {
            continue;
        }
        std::optional<kernel::Proposition> motive = rewrite_context(goal, equality->lhs);
        if (motive.has_value()) {
            return kernel::ProofTerm::equality_elimination(
                equality->type, equality->lhs, equality->rhs, std::move(*motive),
                kernel::ProofTerm::hypothesis(kernel::HypothesisIndex{
                    static_cast<std::uint32_t>(supposed.size() - position)}),
                kernel::ProofTerm::reflexivity());
        }
    }

    return kernel::ProofTerm::reflexivity();
}

}  // namespace

kernel::ProofTerm definitional_evidence(const kernel::Proposition& goal) {
    if (const auto* quantified = std::get_if<kernel::Forall>(&goal.node)) {
        return kernel::ProofTerm::forall_introduction(quantified->binder,
                                                      definitional_evidence(*quantified->body));
    }
    if (const auto* implication = std::get_if<kernel::Implies>(&goal.node)) {
        return kernel::ProofTerm::implication_introduction(
            *implication->premise, definitional_evidence(*implication->conclusion));
    }
    return kernel::ProofTerm::reflexivity();
}

kernel::ProofTerm automatic_evidence(const kernel::Proposition& goal) {
    std::vector<Supposed> supposed;
    return shaped_evidence(goal, supposed, 0);
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
