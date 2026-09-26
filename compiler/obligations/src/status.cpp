#include "cppl/obligations/status.hpp"

#include "cppl/kernel/check.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/vir/ids.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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
        case Origin::LoopDescent:
            return "loop measure descent";
        case Origin::CallDescent:
            return "recursive call measure descent";
        case Origin::RefinementIntroduction:
            return "refinement membership";
        case Origin::ElementBounds:
            return "element index within extent";
        case Origin::OmittedCase:
            return "omitted case is impossible";
        case Origin::ImpossiblePath:
            return "runtime path is unreachable";
        case Origin::DefinedBehavior:
            return "operation has defined behavior";
    }
    return "obligation";
}

kernel::Proposition relative_to(const std::vector<TrustedPremise>& premises, kernel::Proposition goal) {
    for (const TrustedPremise& premise : std::views::reverse(premises)) {
        goal = kernel::Proposition::implication(premise.proposition, std::move(goal));
    }
    return goal;
}

const RefinementPredicate* Program::refinement(std::string_view name) const {
    for (const RefinementPredicate& candidate : refinements) {
        if ((candidate.identity.empty() ? candidate.name : candidate.identity) == name) {
            return &candidate;
        }
    }
    return nullptr;
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

// How a candidate is shaped where the goal or the premises leave a choice. It
// selects a shape; it never decides that a proposition holds, and every shape it
// produces is put to the kernel.
struct Shaping {
    std::vector<Supposed>& supposed;
    // Which side of a disjunctive goal to introduce.
    bool prefer_right = false;
    // Disjunctive premises already split on the way here, so each is split once
    // and the case analysis terminates.
    std::vector<kernel::Proposition> split;
};

// What a premise standing in scope makes available, with the evidence that
// reaches it. A conjunction offers itself and, by conjunction elimination, what
// each of its sides offers in turn.
//
// A disjunction offers only itself: neither side follows from it. Using one is
// the case analysis below.
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
kernel::ProofTerm shaped_evidence(const kernel::Proposition& goal, Shaping& shaping, std::size_t binders,
                                  std::size_t skip = 0) {
    if (const auto* quantified = std::get_if<kernel::Forall>(&goal.node)) {
        return kernel::ProofTerm::forall_introduction(quantified->binder,
                                                      shaped_evidence(*quantified->body, shaping, binders + 1));
    }

    if (const auto* implication = std::get_if<kernel::Implies>(&goal.node)) {
        shaping.supposed.push_back(Supposed{*implication->premise, binders});
        kernel::ProofTerm body = shaped_evidence(*implication->conclusion, shaping, binders);
        shaping.supposed.pop_back();
        return kernel::ProofTerm::implication_introduction(*implication->premise, std::move(body));
    }

    const std::vector<std::pair<kernel::Proposition, kernel::ProofTerm>> available =
        in_scope(shaping.supposed, binders);

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
        return kernel::ProofTerm::conjunction_introduction(shaped_evidence(*conjunction->left, shaping, binders),
                                                           shaped_evidence(*conjunction->right, shaping, binders));
    }

    // A disjunctive goal is established by one of its sides. Which one is not
    // something this candidate can know, so it introduces the side it was shaped
    // for and leaves the decision to the kernel.
    if (const auto* disjunction = std::get_if<kernel::Or>(&goal.node)) {
        const kernel::Proposition& side = shaping.prefer_right ? *disjunction->right : *disjunction->left;
        return kernel::ProofTerm::disjunction_introduction(shaped_evidence(side, shaping, binders),
                                                           shaping.prefer_right);
    }

    // A disjunctive premise is used by proving the goal again under each of its
    // sides. Nothing here resolves the disjunction: both branches are built, and
    // each side is supposed only inside its own branch.
    for (const auto& [proposition, evidence] : available) {
        const auto* disjunction = std::get_if<kernel::Or>(&proposition.node);
        if (disjunction == nullptr || std::ranges::find(shaping.split, proposition) != shaping.split.end()) {
            continue;
        }
        shaping.split.push_back(proposition);
        const auto under = [&](const kernel::Proposition& side) {
            shaping.supposed.push_back(Supposed{side, binders});
            kernel::ProofTerm body = shaped_evidence(goal, shaping, binders);
            shaping.supposed.pop_back();
            return kernel::ProofTerm::implication_introduction(side, std::move(body));
        };
        kernel::ProofTerm left = under(*disjunction->left);
        kernel::ProofTerm right = under(*disjunction->right);
        shaping.split.pop_back();
        return kernel::ProofTerm::disjunction_elimination(proposition, evidence, std::move(left), std::move(right));
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
                                                           shaped_evidence(rewritten, shaping, binders, index + 1));
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
                return kernel::ProofTerm::equality_elimination(equality->type, equality->rhs, equality->lhs,
                                                               std::move(*motive), std::move(symmetry),
                                                               shaped_evidence(rewritten, shaping, binders, index + 1));
            }
        }
    }

    return kernel::ProofTerm::reflexivity();
}

} // namespace

kernel::ProofTerm definitional_evidence(const kernel::Proposition& goal, bool prefer_right) {
    if (const auto* quantified = std::get_if<kernel::Forall>(&goal.node)) {
        return kernel::ProofTerm::forall_introduction(quantified->binder,
                                                      definitional_evidence(*quantified->body, prefer_right));
    }
    if (const auto* implication = std::get_if<kernel::Implies>(&goal.node)) {
        return kernel::ProofTerm::implication_introduction(
            *implication->premise, definitional_evidence(*implication->conclusion, prefer_right));
    }
    if (const auto* conjunction = std::get_if<kernel::And>(&goal.node)) {
        return kernel::ProofTerm::conjunction_introduction(definitional_evidence(*conjunction->left, prefer_right),
                                                           definitional_evidence(*conjunction->right, prefer_right));
    }
    if (const auto* disjunction = std::get_if<kernel::Or>(&goal.node)) {
        const kernel::Proposition& side = prefer_right ? *disjunction->right : *disjunction->left;
        return kernel::ProofTerm::disjunction_introduction(definitional_evidence(side, prefer_right), prefer_right);
    }
    return kernel::ProofTerm::reflexivity();
}

kernel::ProofTerm automatic_evidence(const kernel::Proposition& goal, bool prefer_right) {
    std::vector<Supposed> supposed;
    Shaping shaping{supposed, prefer_right, {}};
    return shaped_evidence(goal, shaping, 0);
}

Verdict Verdict::proven(const kernel::Acceptance& acceptance, const Obligation& obligation,
                        std::vector<TrustedPremise> premises) {
    // The acceptance must be for this obligation's goal, under exactly the
    // premises the verdict will name. Holding an acceptance for some other
    // proposition establishes nothing about this one, and one under premises
    // the verdict does not name would report fewer assumptions than it rests on.
    if (!(acceptance.proposition() == relative_to(premises, obligation.goal))) {
        return {Status::Unresolved, "the kernel accepted a different proposition than this obligation states"};
    }
    return {Status::Proven, {}, std::move(premises)};
}

Verdict Verdict::trusted(std::string declaration) {
    return {Status::Trusted, std::move(declaration)};
}

Verdict Verdict::unresolved(std::string reason) {
    return {Status::Unresolved, std::move(reason)};
}

} // namespace cppl::obligations
