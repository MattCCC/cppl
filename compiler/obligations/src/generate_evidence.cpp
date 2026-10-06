// Evidence for a written proof step: claimed and instantiated propositions,
// and the rewrites and conversions between them.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/kernel/box.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/types.hpp"
#include "definedness.hpp"
#include "generate_detail.hpp"
#include "lowering.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations {

namespace {

using detail::Failure;

using detail::DefinitionMap;

// Whether evidence for `available` can stand as evidence for `goal`.
//
// The quantifier prefix and the type of the equality must agree: a proof term
// restates its binders, so a term built for one prefix is not a term for
// another. Whether the two equalities themselves coincide is left to the
// kernel, which is the point of `apply`.
bool conclusion_is_applicable(const kernel::Proposition& available, const kernel::Proposition& goal,
                              std::string& reason) {
    const auto* available_forall = std::get_if<kernel::Forall>(&available.node);
    const auto* goal_forall = std::get_if<kernel::Forall>(&goal.node);

    if (available_forall != nullptr && goal_forall != nullptr) {
        if (!(available_forall->binder == goal_forall->binder)) {
            reason = "it quantifies over '" + kernel::describe(available_forall->binder) +
                     "' where the goal quantifies over '" + kernel::describe(goal_forall->binder) + "'";
            return false;
        }
        return conclusion_is_applicable(*available_forall->body, *goal_forall->body, reason);
    }

    if (available_forall != nullptr || goal_forall != nullptr) {
        reason = "it quantifies over a different number of variables than the goal";
        return false;
    }

    const auto* available_implies = std::get_if<kernel::Implies>(&available.node);
    const auto* goal_implies = std::get_if<kernel::Implies>(&goal.node);

    if (available_implies != nullptr && goal_implies != nullptr) {
        return conclusion_is_applicable(*available_implies->premise, *goal_implies->premise, reason) &&
               conclusion_is_applicable(*available_implies->conclusion, *goal_implies->conclusion, reason);
    }

    if (available_implies != nullptr || goal_implies != nullptr) {
        reason = available_implies != nullptr ? "it supposes a premise the goal does not"
                                              : "the goal supposes a premise it does not";
        return false;
    }

    const auto* available_and = std::get_if<kernel::And>(&available.node);
    const auto* goal_and = std::get_if<kernel::And>(&goal.node);

    if (available_and != nullptr && goal_and != nullptr) {
        return conclusion_is_applicable(*available_and->left, *goal_and->left, reason) &&
               conclusion_is_applicable(*available_and->right, *goal_and->right, reason);
    }

    if (available_and != nullptr || goal_and != nullptr) {
        reason = available_and != nullptr ? "it states a conjunction the goal does not"
                                          : "the goal states a conjunction it does not";
        return false;
    }

    const auto* available_or = std::get_if<kernel::Or>(&available.node);
    const auto* goal_or = std::get_if<kernel::Or>(&goal.node);

    if (available_or != nullptr && goal_or != nullptr) {
        return conclusion_is_applicable(*available_or->left, *goal_or->left, reason) &&
               conclusion_is_applicable(*available_or->right, *goal_or->right, reason);
    }

    if (available_or != nullptr || goal_or != nullptr) {
        reason = available_or != nullptr ? "it states a disjunction the goal does not"
                                         : "the goal states a disjunction it does not";
        return false;
    }

    const bool available_false = std::holds_alternative<kernel::Falsity>(available.node);
    const bool goal_false = std::holds_alternative<kernel::Falsity>(goal.node);
    if (available_false || goal_false) {
        if (available_false != goal_false) {
            reason = available_false ? "it concludes False where the goal is an equality"
                                     : "the goal is False and it concludes an equality";
        }
        return available_false == goal_false;
    }

    const auto& available_equality = std::get<kernel::Eq>(available.node);
    const auto& goal_equality = std::get<kernel::Eq>(goal.node);
    if (!(available_equality.type == goal_equality.type)) {
        reason = "it is an equality at '" + kernel::describe(available_equality.type) +
                 "' where the goal is an equality at '" + kernel::describe(goal_equality.type) + "'";
        return false;
    }
    return true;
}

} // namespace

namespace detail::generation {

// The proposition a `proves` clause claims.
//
// A law states what holds for every inhabitant of its parameters. Naming it at
// particular arguments claims one instance of it, which is that statement with
// the quantifiers instantiated: universal elimination, performed here on the
// proposition by the kernel's own substitution. What remains open are the
// proof's own parameters, and those are quantified back over the result.
//
// A refined law parameter's membership is part of the law's proposition, so
// the instance supposes it of the argument: the claim never states the law's
// conclusion at a value outside the parameter's type. The proof's parameters
// are quantified back with no membership of their own, so a proof claiming its
// law at its own parameters states exactly the law; a refined proof parameter
// the law does not refine is claimed of every value of its base type, which
// claims more and never less.
std::optional<kernel::Proposition> claimed_proposition(const vir::Proof& proof, const Obligation& obligation,
                                                       const DefinitionMap& definitions, diagnostics::Engine& engine) {
    if (!proof.law)
        return obligation.goal;
    const auto& claim = std::get<vir::Call>(proof.proposition.node);
    TermLowering lowering(definitions, proof.parameters.size());

    kernel::Proposition instance = obligation.goal;
    for (const vir::Expr& argument : claim.arguments) {
        const auto* quantified = std::get_if<kernel::Forall>(&instance.node);
        if (quantified == nullptr) {
            report(engine, diagnostics::Category::UnsupportedSemantics, argument.provenance.range.begin,
                   "law '" + obligation.subject +
                       "' is named at more arguments than it "
                       "quantifies over");
            return std::nullopt;
        }

        // The claim is the law's proposition at the argument's value. An
        // argument whose operations C++ defines only under a condition has no
        // value where the condition fails, and nothing supposes it holds here,
        // so it is refused rather than given the value of the total primitive
        // (SPEC.md ARITH-010).
        if (const auto sites = detail::definedness_sites(argument); !sites.empty()) {
            report(
                engine, diagnostics::Category::UnsupportedSemantics, argument.provenance.range.begin,
                "proof '" + proof.name + "' claims law '" + obligation.subject +
                    "' at an argument whose behavior is not always defined: " + detail::explain(sites.front()).front(),
                "state the argument's condition as a premise of the law instead");
            return std::nullopt;
        }
        std::expected<kernel::Term, Failure> term = lowering.lower(argument);
        if (!term) {
            report(engine, diagnostics::Category::UnsupportedSemantics, term.error().location,
                   "proof '" + proof.name + "' claims law '" + obligation.subject +
                       "' at a term the formal core cannot state: " + term.error().reason);
            return std::nullopt;
        }
        instance = kernel::instantiate(*quantified->body, *term);
    }

    for (auto parameter = proof.parameters.rbegin(); parameter != proof.parameters.rend(); ++parameter) {
        const std::optional<kernel::Type> binder = lower_type(parameter->type);
        if (!binder.has_value()) {
            report(engine, diagnostics::Category::UnsupportedSemantics, proof.range.begin,
                   "proof '" + proof.name + "' quantifies over '" + vir::describe(parameter->type) +
                       "', which the formal core does not represent");
            return std::nullopt;
        }
        instance = kernel::Proposition::for_all(*binder, std::move(instance));
    }

    return instance;
}

} // namespace detail::generation

namespace {

bool mentions_variable(const kernel::Term& term) {
    if (const auto* call = std::get_if<kernel::Call>(&term.node))
        return std::ranges::any_of(call->arguments, [](const auto& argument) { return mentions_variable(argument); });
    if (const auto* primitive = std::get_if<kernel::Prim>(&term.node))
        return std::ranges::any_of(primitive->arguments,
                                   [](const auto& argument) { return mentions_variable(argument); });
    return std::holds_alternative<kernel::Var>(term.node);
}

} // namespace

namespace detail::generation {

// `depth` is how many binders enclose the goal this evidence is being offered
// for. The proof's parameters are the outermost of them, so an argument naming
// one is lowered against that depth rather than against the parameter list.
std::optional<Instantiation> instantiate_evidence(const vir::Proof& proof, const std::string& evidence,
                                                  Instantiation state, const std::vector<vir::Expr>& arguments,
                                                  std::size_t depth, const DefinitionMap& definitions,
                                                  diagnostics::Engine& engine) {
    TermLowering lowering(definitions, depth);

    for (const vir::Expr& argument : arguments) {
        const source::SourceLocation& location = argument.provenance.range.begin;

        const auto* quantified = std::get_if<kernel::Forall>(&state.proposition.node);
        if (quantified == nullptr) {
            report(engine, diagnostics::Category::ProofFailure, location,
                   "proof '" + evidence +
                       "' is instantiated at more arguments than it "
                       "quantifies over",
                   "at this argument it establishes " + kernel::describe(state.proposition) +
                       ", which quantifies over nothing");
            return std::nullopt;
        }

        const std::optional<kernel::Type> type = lower_type(argument.type);
        if (!type.has_value() || !(*type == quantified->binder)) {
            report(engine, diagnostics::Category::ProofFailure, location,
                   "proof '" + evidence + "' quantifies over '" + kernel::describe(quantified->binder) +
                       "' and cannot be instantiated at a term of type '" + vir::describe(argument.type) + "'");
            return std::nullopt;
        }

        std::expected<kernel::Term, Failure> term = lowering.lower(argument);
        if (!term) {
            report(engine, diagnostics::Category::UnsupportedSemantics, term.error().location,
                   "proof '" + proof.name + "' instantiates '" + evidence +
                       "' at a term the formal core cannot state: " + term.error().reason);
            return std::nullopt;
        }

        kernel::Proposition eliminated = kernel::instantiate(*quantified->body, *term);
        state.open = state.open || mentions_variable(*term);
        state.term = kernel::ProofTerm::forall_elimination(std::move(state.proposition), std::move(state.term), *term);
        state.proposition = std::move(eliminated);
    }

    return state;
}

// The statement a goal makes underneath the quantifiers it leads with, and the
// binders passed on the way.
const kernel::Proposition* under_quantifiers(const kernel::Proposition& goal, std::vector<kernel::Type>& binders) {
    const kernel::Proposition* inner = &goal;
    while (const auto* quantified = std::get_if<kernel::Forall>(&inner->node)) {
        binders.push_back(quantified->binder);
        inner = &*quantified->body;
    }
    return inner;
}

} // namespace detail::generation

namespace {

kernel::Term abstract_occurrences(const kernel::Term& term, const kernel::Term& target, std::uint32_t depth,
                                  bool& found);

kernel::Proposition abstract_occurrences(const kernel::Proposition& proposition, const kernel::Term& target,
                                         std::uint32_t depth, bool& found) {
    if (const auto* quantified = std::get_if<kernel::Forall>(&proposition.node)) {
        return kernel::Proposition::for_all(
            quantified->binder, abstract_occurrences(*quantified->body, kernel::shift(target, 1), depth + 1, found));
    }
    if (const auto* implication = std::get_if<kernel::Implies>(&proposition.node)) {
        return kernel::Proposition::implication(abstract_occurrences(*implication->premise, target, depth, found),
                                                abstract_occurrences(*implication->conclusion, target, depth, found));
    }
    if (const auto* conjunction = std::get_if<kernel::And>(&proposition.node)) {
        return kernel::Proposition::conjunction(abstract_occurrences(*conjunction->left, target, depth, found),
                                                abstract_occurrences(*conjunction->right, target, depth, found));
    }
    if (const auto* disjunction = std::get_if<kernel::Or>(&proposition.node)) {
        return kernel::Proposition::disjunction(abstract_occurrences(*disjunction->left, target, depth, found),
                                                abstract_occurrences(*disjunction->right, target, depth, found));
    }
    if (std::holds_alternative<kernel::Falsity>(proposition.node)) {
        return proposition;
    }
    const auto& equality = std::get<kernel::Eq>(proposition.node);
    return kernel::Proposition::equality(equality.type, abstract_occurrences(equality.lhs, target, depth, found),
                                         abstract_occurrences(equality.rhs, target, depth, found));
}

kernel::Term abstract_occurrences(const kernel::Term& term, const kernel::Term& target, std::uint32_t depth,
                                  bool& found) {
    if (term == target) {
        found = true;
        return kernel::Term::variable(kernel::VarIndex{depth});
    }
    if (const auto* call = std::get_if<kernel::Call>(&term.node)) {
        std::vector<kernel::Term> arguments;
        arguments.reserve(call->arguments.size());
        for (const kernel::Term& argument : call->arguments) {
            arguments.push_back(abstract_occurrences(argument, target, depth, found));
        }
        return kernel::Term::call(call->callee, std::move(arguments));
    }
    if (const auto* primitive = std::get_if<kernel::Prim>(&term.node)) {
        std::vector<kernel::Term> arguments;
        arguments.reserve(primitive->arguments.size());
        for (const kernel::Term& argument : primitive->arguments) {
            arguments.push_back(abstract_occurrences(argument, target, depth, found));
        }
        return kernel::Term::primitive(primitive->op, primitive->type, std::move(arguments));
    }
    return term;
}

} // namespace

namespace detail::generation {

// The context a rewrite transports through: the goal with every occurrence of
// `target` standing for the hole.
//
// Which occurrences a rewrite transforms is a question about what the author
// meant, so it is settled here rather than in the kernel. Every occurrence is
// the rule, and it is the whole rule: nothing is searched for and nothing is
// weighed. The context is then handed to the kernel as part of the proof term,
// and the kernel checks that filling it yields the goal, so a choice made here
// can only fail to prove something - never prove the wrong thing.
std::optional<kernel::Proposition> make_rewrite_context(const kernel::Proposition& goal, const kernel::Term& target) {
    // The context stands underneath one more binder than the goal does - the
    // hole itself - so the goal is restated for that depth before the
    // occurrences are taken out of it.
    bool found = false;
    kernel::Proposition motive = abstract_occurrences(kernel::shift(goal, 1), kernel::shift(target, 1), 0, found);
    if (!found) {
        return std::nullopt;
    }
    return motive;
}

kernel::ProofTerm quantify(const std::vector<kernel::Type>& binders, kernel::ProofTerm term) {
    for (const auto& binder : std::views::reverse(binders)) {
        term = kernel::ProofTerm::forall_introduction(binder, std::move(term));
    }
    return term;
}

// How many premises stand between a statement's evidence and the goal.
//
// Deciding this needs the two propositions and nothing else, so it is settled
// before any statement is consumed to discharge them. `exact` offers the goal
// itself and so discharges nothing: peeling a premise is what `apply` means.
bool convertible_equality(const kernel::Context& context, const kernel::Proposition& available,
                          const kernel::Proposition& goal) {
    const auto* from = std::get_if<kernel::Eq>(&available.node);
    const auto* to = std::get_if<kernel::Eq>(&goal.node);
    if (from == nullptr || to == nullptr || !(from->type == to->type))
        return false;
    const auto left_from = kernel::normalize(context, from->lhs, kernel::CoreLimits{});
    const auto left_to = kernel::normalize(context, to->lhs, kernel::CoreLimits{});
    const auto right_from = kernel::normalize(context, from->rhs, kernel::CoreLimits{});
    const auto right_to = kernel::normalize(context, to->rhs, kernel::CoreLimits{});
    return left_from && left_to && right_from && right_to && *left_from == *left_to && *right_from == *right_to;
}

// Definitional conversion is derived from the existing equality rule. Both
// conversions are explicit reflexivity evidence, checked independently by the
// kernel; normalization in the producer merely chooses when to offer them.
kernel::ProofTerm convert_equality(const kernel::Proposition& available, const kernel::Proposition& goal,
                                   kernel::ProofTerm evidence) {
    const auto* from = std::get_if<kernel::Eq>(&available.node);
    const auto* to = std::get_if<kernel::Eq>(&goal.node);
    if (available == goal || from == nullptr || to == nullptr || !(from->type == to->type))
        return evidence;
    const auto hole = kernel::Term::variable(kernel::VarIndex{0});
    auto right = kernel::ProofTerm::equality_elimination(
        to->type, to->rhs, from->rhs, kernel::Proposition::equality(to->type, kernel::shift(from->lhs, 1), hole),
        kernel::ProofTerm::reflexivity(), std::move(evidence));
    return kernel::ProofTerm::equality_elimination(
        to->type, to->lhs, from->lhs, kernel::Proposition::equality(to->type, hole, kernel::shift(to->rhs, 1)),
        kernel::ProofTerm::reflexivity(), std::move(right));
}

std::optional<std::size_t> premises_before_the_goal(const kernel::Context& context,
                                                    const kernel::Proposition& available,
                                                    const kernel::Proposition& goal, bool exact, std::string& reason) {
    if (exact) {
        return available == goal || convertible_equality(context, available, goal) ? std::optional<std::size_t>{0}
                                                                                   : std::nullopt;
    }

    const kernel::Proposition* current = &available;
    std::size_t discharged = 0;
    while (true) {
        if (conclusion_is_applicable(*current, goal, reason)) {
            return discharged;
        }
        const auto* implication = std::get_if<kernel::Implies>(&current->node);
        if (implication == nullptr) {
            return std::nullopt;
        }
        current = &*implication->conclusion;
        ++discharged;
    }
}

} // namespace detail::generation

} // namespace cppl::obligations
