#include "cppl/obligations/status.hpp"

#include "cppl/kernel/substitution.hpp"

#include <algorithm>
#include <limits>
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
        case Origin::CallPrecondition:
            return "call-site precondition";
        case Origin::ReturnPath:
            return "return path";
        case Origin::FunctionContract:
            return "function contract";
        case Origin::LawProposition:
            return "law proposition";
        case Origin::ProofProposition:
            return "proof proposition";
        case Origin::LoopEntry:
            return "loop invariant on entry";
        case Origin::LoopPreservation:
            return "loop invariant preservation";
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
    if (obligation.proof) {
        for (const auto& proof : proofs) {
            if (proof.id == *obligation.proof)
                return &proof;
        }
        return nullptr;
    }
    return obligation.law.has_value() ? proof_for(*obligation.law) : nullptr;
}

bool Program::proof_refused(const Obligation& obligation) const {
    // A direct proposition is always accompanied by a written proof. There is
    // no fallback strategy when its evidence failed to elaborate.
    if (obligation.proof)
        return proof_for(obligation) == nullptr;
    return obligation.law.has_value() && proof_refused(*obligation.law);
}

namespace {

// A premise supposed on the way into a goal, with the number of binders that
// stood over it at the time.
struct Supposed {
    kernel::Proposition proposition;
    std::size_t binders = 0;
};

// What a premise standing in scope makes available, with the evidence that
// reaches it. A conjunction offers itself and, by conjunction elimination, what
// each of its sides offers in turn.
void reachable(const kernel::Proposition& premise, kernel::ProofTerm evidence,
               std::vector<std::pair<kernel::Proposition, kernel::ProofTerm>>& available) {
    available.emplace_back(premise, evidence);
    if (const auto* conjunction = std::get_if<kernel::And>(&premise.node)) {
        reachable(*conjunction->left, kernel::ProofTerm::conjunction_elimination(premise, evidence, false), available);
        reachable(*conjunction->right, kernel::ProofTerm::conjunction_elimination(premise, std::move(evidence), true),
                  available);
    }
}

// Every proposition the supposed premises make available at this depth,
// innermost premise first.
std::vector<std::pair<kernel::Proposition, kernel::ProofTerm>> in_scope(const std::vector<Supposed>& supposed,
                                                                        std::size_t binders) {
    std::vector<std::pair<kernel::Proposition, kernel::ProofTerm>> available;
    for (std::size_t position = supposed.size(); position > 0; --position) {
        const Supposed& entry = supposed[position - 1];
        reachable(kernel::shift(entry.proposition, static_cast<std::uint32_t>(binders - entry.binders)),
                  kernel::ProofTerm::hypothesis(
                      kernel::HypothesisIndex{static_cast<std::uint32_t>(supposed.size() - position)}),
                  available);
    }
    return available;
}

// `skip` is how many of the available propositions have already been rewritten
// with on the way here. Each rewrite may only use one standing further out than
// the last, so the sequence of rewrites is finite.
kernel::ProofTerm shaped_evidence(const kernel::Proposition& goal, std::vector<Supposed>& supposed, std::size_t binders,
                                  std::size_t skip = 0) {
    if (const auto* quantified = std::get_if<kernel::Forall>(&goal.node)) {
        return kernel::ProofTerm::forall_introduction(quantified->binder,
                                                      shaped_evidence(*quantified->body, supposed, binders + 1));
    }

    if (const auto* implication = std::get_if<kernel::Implies>(&goal.node)) {
        supposed.push_back(Supposed{*implication->premise, binders});
        kernel::ProofTerm body = shaped_evidence(*implication->conclusion, supposed, binders);
        supposed.pop_back();
        return kernel::ProofTerm::implication_introduction(*implication->premise, std::move(body));
    }

    const std::vector<std::pair<kernel::Proposition, kernel::ProofTerm>> available = in_scope(supposed, binders);

    // A goal that is exactly a premise supposed on the way in, or a side of one,
    // is closed by that premise, innermost first. This is a structural match
    // against what the goal itself introduced, not a search: no equality is used
    // to transform anything, and nothing outside the goal is consulted.
    for (const auto& [proposition, evidence] : available) {
        if (proposition == goal) {
            return evidence;
        }
    }

    // Each side of a conjunction is a goal in its own right, and the premises
    // standing here are available to both.
    if (const auto* conjunction = std::get_if<kernel::And>(&goal.node)) {
        return kernel::ProofTerm::conjunction_introduction(shaped_evidence(*conjunction->left, supposed, binders),
                                                           shaped_evidence(*conjunction->right, supposed, binders));
    }

    for (std::size_t index = skip; index < available.size(); ++index) {
        const auto& [proposition, evidence] = available[index];
        const auto* equality = std::get_if<kernel::Eq>(&proposition.node);
        if (equality == nullptr) {
            continue;
        }
        std::optional<kernel::Proposition> motive = rewrite_context(goal, equality->lhs);
        if (motive.has_value()) {
            const auto rewritten = kernel::instantiate(*motive, equality->rhs);
            return kernel::ProofTerm::equality_elimination(equality->type, equality->lhs, equality->rhs,
                                                           std::move(*motive), evidence,
                                                           shaped_evidence(rewritten, supposed, binders, index + 1));
        }
        if (!(equality->type == kernel::Type{kernel::kBoolean})) {
            motive = rewrite_context(goal, equality->rhs);
            if (motive.has_value()) {
                auto symmetry = kernel::ProofTerm::equality_elimination(
                    equality->type, equality->lhs, equality->rhs,
                    kernel::Proposition::equality(equality->type, kernel::shift(equality->rhs, 1),
                                                  kernel::Term::variable(kernel::VarIndex{0})),
                    evidence, kernel::ProofTerm::reflexivity());
                const auto rewritten = kernel::instantiate(*motive, equality->lhs);
                return kernel::ProofTerm::equality_elimination(
                    equality->type, equality->rhs, equality->lhs, std::move(*motive), std::move(symmetry),
                    shaped_evidence(rewritten, supposed, binders, index + 1));
            }
        }
    }

    return kernel::ProofTerm::reflexivity();
}

} // namespace

kernel::ProofTerm definitional_evidence(const kernel::Proposition& goal) {
    if (const auto* quantified = std::get_if<kernel::Forall>(&goal.node)) {
        return kernel::ProofTerm::forall_introduction(quantified->binder, definitional_evidence(*quantified->body));
    }
    if (const auto* implication = std::get_if<kernel::Implies>(&goal.node)) {
        return kernel::ProofTerm::implication_introduction(*implication->premise,
                                                           definitional_evidence(*implication->conclusion));
    }
    if (const auto* conjunction = std::get_if<kernel::And>(&goal.node)) {
        return kernel::ProofTerm::conjunction_introduction(definitional_evidence(*conjunction->left),
                                                           definitional_evidence(*conjunction->right));
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
        return {Status::Unresolved, "the kernel accepted a different proposition than this obligation states"};
    }
    return {Status::Proven, {}};
}

Verdict Verdict::unresolved(std::string reason) {
    return {Status::Unresolved, std::move(reason)};
}

} // namespace cppl::obligations
