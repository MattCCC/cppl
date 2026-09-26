#include "cppl/kernel/check.hpp"

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/linear.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::kernel {

namespace {

std::unexpected<Rejection> reject(RejectionKind kind, std::string detail) {
    return std::unexpected(Rejection{kind, std::move(detail)});
}

// A premise standing in the proof context, with the number of term binders that
// enclosed it when it was introduced.
//
// A proposition assumed outside a quantifier states something different
// underneath it, so it is restated for the depth it is used at rather than
// being compared as it was written.
struct Assumption {
    Proposition proposition;
    std::size_t binders = 0;
};

// A goal is checked only after it is known to be a well-formed proposition:
// both sides of every equality must type-check at the stated type under the
// binders that enclose them.
[[nodiscard]] std::expected<void, Rejection> validate_proposition(const Context& context, std::vector<Type>& locals,
                                                                  const Proposition& proposition,
                                                                  const CoreLimits& limits, std::uint32_t depth) {
    if (depth > limits.max_term_depth) {
        return reject(RejectionKind::MalformedProposition, "proposition nests deeper than the core allows");
    }

    if (const auto* quantified = std::get_if<Forall>(&proposition.node)) {
        // Even an unused binder must denote an admitted type. Checking only
        // the body's terms would let malformed quantifiers enter Acceptance.
        if (!is_supported(quantified->binder)) {
            return reject(RejectionKind::MalformedProposition, "quantifier has an unsupported binder type");
        }
        locals.push_back(quantified->binder);
        auto body = validate_proposition(context, locals, *quantified->body, limits, depth + 1);
        locals.pop_back();
        return body;
    }

    // An implication binds nothing, so both sides are stated under the binders
    // that enclose the implication itself.
    if (const auto* implication = std::get_if<Implies>(&proposition.node)) {
        if (auto premise = validate_proposition(context, locals, *implication->premise, limits, depth + 1); !premise) {
            return premise;
        }
        return validate_proposition(context, locals, *implication->conclusion, limits, depth + 1);
    }

    // A conjunction binds nothing either.
    if (const auto* conjunction = std::get_if<And>(&proposition.node)) {
        if (auto left = validate_proposition(context, locals, *conjunction->left, limits, depth + 1); !left) {
            return left;
        }
        return validate_proposition(context, locals, *conjunction->right, limits, depth + 1);
    }

    // Nor a disjunction.
    if (const auto* disjunction = std::get_if<Or>(&proposition.node)) {
        if (auto left = validate_proposition(context, locals, *disjunction->left, limits, depth + 1); !left) {
            return left;
        }
        return validate_proposition(context, locals, *disjunction->right, limits, depth + 1);
    }

    // Falsity states nothing about any term, so there is nothing to type.
    if (std::holds_alternative<Falsity>(proposition.node)) {
        return {};
    }

    const auto& equality = std::get<Eq>(proposition.node);
    for (const Term* side : {&equality.lhs, &equality.rhs}) {
        auto type = type_of(context, locals, *side, limits);
        if (!type) {
            return reject(RejectionKind::MalformedProposition,
                          describe(type.error().kind) + ": " + type.error().detail);
        }
        if (!(*type == equality.type)) {
            return reject(RejectionKind::MalformedProposition, "equality is stated at " + describe(equality.type) +
                                                                   " but one side has type " + describe(*type));
        }
    }
    return {};
}

[[nodiscard]] std::expected<void, Rejection> check_under(const Context& context, std::vector<Type>& locals,
                                                         std::vector<Assumption>& assumptions,
                                                         const Proposition& proposition, const ProofTerm& proof,
                                                         const CoreLimits& limits, std::uint32_t depth) {
    if (depth > limits.max_term_depth) {
        return reject(RejectionKind::MalformedProofTerm, "proof term nests deeper than the core allows");
    }

    if (const auto* branch = std::get_if<ConditionalElimination>(&proof.node)) {
        if (!branch->type.is_integer()) {
            return reject(RejectionKind::MalformedProofTerm, "conditional result is not an integer");
        }
        const Term selected = Term::primitive(PrimOp::Select, branch->type.integer_type(),
                                              {branch->condition, branch->when_true, branch->when_false});
        const auto type = type_of(context, locals, selected, limits);
        if (!type || !(*type == branch->type)) {
            return reject(RejectionKind::MalformedProofTerm, "ill-typed conditional term");
        }
        locals.push_back(branch->type);
        auto motive = validate_proposition(context, locals, *branch->motive, limits, depth + 1);
        locals.pop_back();
        if (!motive)
            return motive;
        if (!(instantiate(*branch->motive, selected) == proposition)) {
            return reject(RejectionKind::ProofShapeMismatch, "conditional motive does not yield the goal");
        }
        const auto true_goal = Proposition::implication(predicate(branch->condition, true),
                                                        instantiate(*branch->motive, branch->when_true));
        const auto false_goal = Proposition::implication(predicate(branch->condition, false),
                                                         instantiate(*branch->motive, branch->when_false));
        if (auto checked = check_under(context, locals, assumptions, true_goal, *branch->true_case, limits, depth + 1);
            !checked) {
            return checked;
        }
        return check_under(context, locals, assumptions, false_goal, *branch->false_case, limits, depth + 1);
    }

    // Linear arithmetic closes an equality from facts whose evidence is itself
    // checked here. The kernel states the facts and the goal's negation as
    // integer constraints on its own, and the certificate must refute them. For
    // `False` the negation states nothing, so the facts alone must be refuted.
    if (const auto* arithmetic = std::get_if<LinearArithmetic>(&proof.node)) {
        if (!std::holds_alternative<Eq>(proposition.node) && !std::holds_alternative<Falsity>(proposition.node)) {
            return reject(RejectionKind::ProofShapeMismatch,
                          "linear arithmetic establishes an equality, a comparison or False, and the "
                          "goal is " +
                              describe(proposition));
        }
        if (arithmetic->facts.size() > limits.max_arithmetic_facts) {
            return reject(RejectionKind::MalformedProofTerm, "an arithmetic step uses more facts than the core allows");
        }
        std::vector<Proposition> facts;
        facts.reserve(arithmetic->facts.size());
        for (const ArithmeticFact& fact : arithmetic->facts) {
            if (auto well_formed = validate_proposition(context, locals, fact.proposition, limits, depth + 1);
                !well_formed) {
                return well_formed;
            }
            if (!std::holds_alternative<Eq>(fact.proposition.node)) {
                return reject(RejectionKind::MalformedProofTerm,
                              "an arithmetic fact must be an equality or a comparison, and " +
                                  describe(fact.proposition) + " is neither");
            }
            if (auto checked =
                    check_under(context, locals, assumptions, fact.proposition, *fact.evidence, limits, depth + 1);
                !checked) {
                return checked;
            }
            facts.push_back(fact.proposition);
        }
        const auto system = arithmetic_system(context, facts, proposition, limits, locals);
        if (!system) {
            return reject(RejectionKind::CoreFailure, describe(system.error().kind) + ": " + system.error().detail);
        }
        if (auto refuted = refutes(*system, arithmetic->certificate, limits); !refuted) {
            return reject(RejectionKind::ProofShapeMismatch,
                          "the arithmetic certificate does not refute the negation of " + describe(proposition) + ": " +
                              refuted.error());
        }
        return {};
    }

    // Universal elimination closes a goal of any shape, because instantiating
    // quantified evidence can leave either an equality or a smaller quantifier.
    // It is therefore decided on the evidence rather than on the goal, and the
    // proposition it yields is derived here and compared with the goal.
    if (const auto* elimination = std::get_if<ForallElimination>(&proof.node)) {
        if (auto well_formed = validate_proposition(context, locals, *elimination->quantified, limits, depth + 1);
            !well_formed) {
            return well_formed;
        }

        const auto* eliminated = std::get_if<Forall>(&elimination->quantified->node);
        if (eliminated == nullptr) {
            return reject(RejectionKind::ProofShapeMismatch, "an argument was applied to evidence for " +
                                                                 describe(*elimination->quantified) +
                                                                 ", which quantifies over nothing");
        }

        if (auto evidence = check_under(context, locals, assumptions, *elimination->quantified, *elimination->evidence,
                                        limits, depth + 1);
            !evidence) {
            return evidence;
        }

        auto argument = type_of(context, locals, elimination->argument, limits);
        if (!argument) {
            return reject(RejectionKind::MalformedProofTerm,
                          describe(argument.error().kind) + ": " + argument.error().detail);
        }
        if (!(*argument == eliminated->binder)) {
            return reject(RejectionKind::ProofShapeMismatch,
                          "evidence quantifying over " + describe(eliminated->binder) + " is instantiated at " +
                              describe(context, elimination->argument) + ", which has type " + describe(*argument));
        }

        const Proposition instantiated = instantiate(*eliminated->body, elimination->argument);
        if (!(instantiated == proposition)) {
            return reject(RejectionKind::ProofShapeMismatch, "instantiating that evidence establishes " +
                                                                 describe(instantiated) + ", which is not the goal " +
                                                                 describe(proposition));
        }
        return {};
    }

    // Implication elimination closes a goal of any shape for the same reason,
    // and is decided on the evidence it discharges rather than on the goal.
    if (const auto* application = std::get_if<ImplicationElimination>(&proof.node)) {
        if (auto well_formed = validate_proposition(context, locals, *application->implication, limits, depth + 1);
            !well_formed) {
            return well_formed;
        }

        const auto* implication = std::get_if<Implies>(&application->implication->node);
        if (implication == nullptr) {
            return reject(RejectionKind::ProofShapeMismatch, "a premise was discharged against evidence for " +
                                                                 describe(*application->implication) +
                                                                 ", which is not an implication");
        }

        if (auto evidence = check_under(context, locals, assumptions, *application->implication, *application->evidence,
                                        limits, depth + 1);
            !evidence) {
            return evidence;
        }

        if (auto premise = check_under(context, locals, assumptions, *implication->premise, *application->premise,
                                       limits, depth + 1);
            !premise) {
            return premise;
        }

        if (!(*implication->conclusion == proposition)) {
            return reject(RejectionKind::ProofShapeMismatch, "discharging that premise establishes " +
                                                                 describe(*implication->conclusion) +
                                                                 ", which is not the goal " + describe(proposition));
        }
        return {};
    }

    // Conjunction elimination closes a goal of any shape: the side taken from
    // the restated conjunction decides what it establishes, not the goal.
    if (const auto* taken = std::get_if<ConjunctionElimination>(&proof.node)) {
        if (auto well_formed = validate_proposition(context, locals, *taken->conjunction, limits, depth + 1);
            !well_formed) {
            return well_formed;
        }

        const auto* conjunction = std::get_if<And>(&taken->conjunction->node);
        if (conjunction == nullptr) {
            return reject(RejectionKind::ProofShapeMismatch, "a side was taken from evidence for " +
                                                                 describe(*taken->conjunction) +
                                                                 ", which is not a conjunction");
        }

        if (auto evidence =
                check_under(context, locals, assumptions, *taken->conjunction, *taken->evidence, limits, depth + 1);
            !evidence) {
            return evidence;
        }

        const Proposition& side = taken->right ? *conjunction->right : *conjunction->left;
        if (!(side == proposition)) {
            return reject(RejectionKind::ProofShapeMismatch, "that side of the conjunction is " + describe(side) +
                                                                 ", which is not the goal " + describe(proposition));
        }
        return {};
    }

    // Disjunction elimination closes a goal of any shape: what it establishes is
    // whatever both cases establish, so the goal is checked once under each side
    // and the disjunction itself is never resolved to one of them.
    if (const auto* cases = std::get_if<DisjunctionElimination>(&proof.node)) {
        if (auto well_formed = validate_proposition(context, locals, *cases->disjunction, limits, depth + 1);
            !well_formed) {
            return well_formed;
        }

        const auto* disjunction = std::get_if<Or>(&cases->disjunction->node);
        if (disjunction == nullptr) {
            return reject(RejectionKind::ProofShapeMismatch, "cases were taken on evidence for " +
                                                                 describe(*cases->disjunction) +
                                                                 ", which is not a disjunction");
        }

        if (auto evidence =
                check_under(context, locals, assumptions, *cases->disjunction, *cases->evidence, limits, depth + 1);
            !evidence) {
            return evidence;
        }

        // Each case is evidence that its own side is enough for the goal. They
        // are ordinary implications, so the premise a case may use is the one an
        // enclosing introduction puts in the context, exactly as anywhere else.
        const Proposition from_left = Proposition::implication(*disjunction->left, proposition);
        if (auto left = check_under(context, locals, assumptions, from_left, *cases->left_case, limits, depth + 1);
            !left) {
            return left;
        }
        const Proposition from_right = Proposition::implication(*disjunction->right, proposition);
        return check_under(context, locals, assumptions, from_right, *cases->right_case, limits, depth + 1);
    }

    // Falsity elimination closes a goal of any shape, and asks nothing of it: the
    // goal is well formed wherever this is reached, and evidence for `False` is
    // checked against `False` itself, never against the goal. A goal of `False`
    // is closed this way only from evidence that already establishes `False`.
    if (const auto* absurd = std::get_if<FalsityElimination>(&proof.node)) {
        return check_under(context, locals, assumptions, Proposition::falsity(), *absurd->evidence, limits, depth + 1);
    }

    // Equality elimination closes a goal of any shape too: the context it
    // transports through decides what the result says, not the goal.
    if (const auto* transport = std::get_if<EqualityElimination>(&proof.node)) {
        const Proposition equality = Proposition::equality(transport->type, transport->lhs, transport->rhs);
        if (auto well_formed = validate_proposition(context, locals, equality, limits, depth + 1); !well_formed) {
            return well_formed;
        }
        if (auto checked = check_under(context, locals, assumptions, equality, *transport->equality, limits, depth + 1);
            !checked) {
            return checked;
        }

        // The hole stands for a term of the type the equality is stated at, so
        // the context is checked under exactly that binder. A hole standing
        // where some other type is required is not a context for this equality.
        locals.push_back(transport->type);
        auto motive = validate_proposition(context, locals, *transport->motive, limits, depth + 1);
        locals.pop_back();
        if (!motive) {
            return motive;
        }

        const Proposition transported = instantiate(*transport->motive, transport->rhs);
        if (auto checked =
                check_under(context, locals, assumptions, transported, *transport->evidence, limits, depth + 1);
            !checked) {
            return checked;
        }

        const Proposition result = instantiate(*transport->motive, transport->lhs);
        if (!(result == proposition)) {
            return reject(RejectionKind::ProofShapeMismatch, "transporting that evidence establishes " +
                                                                 describe(result) + ", which is not the goal " +
                                                                 describe(proposition));
        }
        return {};
    }

    // A hypothesis stands for a premise an enclosing introduction placed in the
    // context. The kernel holds that context itself, so evidence can never name
    // a premise that is not there.
    if (const auto* assumed = std::get_if<Hypothesis>(&proof.node)) {
        if (assumed->index.value >= assumptions.size()) {
            return reject(RejectionKind::MalformedProofTerm,
                          "evidence names hypothesis " + std::to_string(assumed->index.value) + ", and " +
                              std::to_string(assumptions.size()) + " premises are in scope");
        }

        const Assumption& assumption = assumptions[assumptions.size() - 1 - assumed->index.value];
        const Proposition available =
            shift(assumption.proposition, static_cast<std::uint32_t>(locals.size() - assumption.binders));
        if (!(available == proposition)) {
            return reject(RejectionKind::ProofShapeMismatch, "that hypothesis is " + describe(available) +
                                                                 ", which is not the goal " + describe(proposition));
        }
        return {};
    }

    if (const auto* quantified = std::get_if<Forall>(&proposition.node)) {
        const auto* introduction = std::get_if<ForallIntroduction>(&proof.node);
        if (introduction == nullptr) {
            return reject(RejectionKind::ProofShapeMismatch,
                          "a universally quantified goal requires forall-introduction");
        }
        if (!(introduction->binder == quantified->binder)) {
            return reject(RejectionKind::ProofShapeMismatch,
                          "evidence introduces a binder of type " + describe(introduction->binder) +
                              " but the goal quantifies over " + describe(quantified->binder));
        }

        locals.push_back(quantified->binder);
        auto body =
            check_under(context, locals, assumptions, *quantified->body, *introduction->body, limits, depth + 1);
        locals.pop_back();
        return body;
    }

    // An implication is introduced by assuming its premise. The premise becomes
    // available to the evidence for the conclusion and to nothing else: it is
    // popped again here, and the kernel never treats it as established.
    if (const auto* implication = std::get_if<Implies>(&proposition.node)) {
        const auto* introduction = std::get_if<ImplicationIntroduction>(&proof.node);
        if (introduction == nullptr) {
            return reject(RejectionKind::ProofShapeMismatch, "an implication goal requires implication-introduction");
        }
        if (!(*introduction->premise == *implication->premise)) {
            return reject(RejectionKind::ProofShapeMismatch, "evidence assumes " + describe(*introduction->premise) +
                                                                 " but the goal supposes " +
                                                                 describe(*implication->premise));
        }

        assumptions.push_back(Assumption{*implication->premise, locals.size()});
        auto body =
            check_under(context, locals, assumptions, *implication->conclusion, *introduction->body, limits, depth + 1);
        assumptions.pop_back();
        return body;
    }

    // A conjunction is introduced by establishing each side. Both sides come
    // from the goal, so evidence cannot substitute a weaker pair.
    if (const auto* conjunction = std::get_if<And>(&proposition.node)) {
        const auto* introduction = std::get_if<ConjunctionIntroduction>(&proof.node);
        if (introduction == nullptr) {
            return reject(RejectionKind::ProofShapeMismatch, "a conjunction goal requires conjunction-introduction");
        }
        if (auto left =
                check_under(context, locals, assumptions, *conjunction->left, *introduction->left, limits, depth + 1);
            !left) {
            return left;
        }
        return check_under(context, locals, assumptions, *conjunction->right, *introduction->right, limits, depth + 1);
    }

    // A disjunction is introduced by establishing one of its sides. The side is
    // selected by the evidence but stated by the goal, so a proof cannot widen
    // the goal to a side it finds easier.
    if (const auto* disjunction = std::get_if<Or>(&proposition.node)) {
        const auto* introduction = std::get_if<DisjunctionIntroduction>(&proof.node);
        if (introduction == nullptr) {
            return reject(RejectionKind::ProofShapeMismatch,
                          "a disjunction goal is established by introducing one of its sides, or by "
                          "eliminating evidence for it");
        }
        const Proposition& side = introduction->right ? *disjunction->right : *disjunction->left;
        return check_under(context, locals, assumptions, side, *introduction->evidence, limits, depth + 1);
    }

    // `False` has no introduction rule. Every way to establish it has been tried
    // above: a hypothesis supposing it, an elimination that yields it, or linear
    // arithmetic refuting the facts alone.
    if (std::holds_alternative<Falsity>(proposition.node)) {
        return reject(RejectionKind::ProofShapeMismatch,
                      "False has no introduction; it is established only by a hypothesis, by eliminating evidence "
                      "for it, or by linear arithmetic refuting the facts alone");
    }

    const auto& equality = std::get<Eq>(proposition.node);
    if (!std::holds_alternative<Reflexivity>(proof.node)) {
        return reject(RejectionKind::ProofShapeMismatch,
                      "an equality goal is established by reflexivity, by a hypothesis, or by "
                      "eliminating evidence for it");
    }

    auto lhs = normalize(context, equality.lhs, limits);
    if (!lhs) {
        return reject(RejectionKind::CoreFailure, describe(lhs.error().kind) + ": " + lhs.error().detail);
    }
    auto rhs = normalize(context, equality.rhs, limits);
    if (!rhs) {
        return reject(RejectionKind::CoreFailure, describe(rhs.error().kind) + ": " + rhs.error().detail);
    }

    if (!(*lhs == *rhs)) {
        return reject(RejectionKind::NotDefinitionallyEqual,
                      "reflexivity requires definitionally equal terms, but " + describe(context, equality.lhs) +
                          " reduces to " + describe(context, *lhs) + " while " + describe(context, equality.rhs) +
                          " reduces to " + describe(context, *rhs));
    }
    return {};
}

} // namespace

std::string describe(RejectionKind kind) {
    switch (kind) {
        case RejectionKind::MalformedProposition:
            return "malformed proposition";
        case RejectionKind::MalformedProofTerm:
            return "malformed proof term";
        case RejectionKind::ProofShapeMismatch:
            return "proof shape mismatch";
        case RejectionKind::NotDefinitionallyEqual:
            return "terms are not definitionally equal";
        case RejectionKind::CoreFailure:
            return "core failure";
    }
    return "unknown rejection";
}

std::expected<Acceptance, Rejection> check(const Context& context, const Proposition& proposition,
                                           const ProofTerm& proof, const CoreLimits& limits) {
    std::vector<Type> locals;

    if (auto well_formed = validate_proposition(context, locals, proposition, limits, 0); !well_formed) {
        return std::unexpected(well_formed.error());
    }

    // Nothing is assumed to begin with. Every premise a proof uses has to have
    // been introduced by the proof itself.
    locals.clear();
    std::vector<Assumption> assumptions;
    if (auto checked = check_under(context, locals, assumptions, proposition, proof, limits, 0); !checked) {
        return std::unexpected(checked.error());
    }

    return Acceptance{proposition};
}

} // namespace cppl::kernel
