// Derivations by the structural rules: hypotheses, quantifiers,
// implication, conjunction, and induction.

#include "cppl/automation/evidence.hpp"
#include "cppl/kernel/box.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/kernel_generator.hpp"
#include "kernel_generator_detail.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::testing::kernel_generator {

using detail::Derivation;
using detail::Generator;
using detail::Hypothesis;
using detail::indexed_a;
using detail::kRuleCount;
using detail::Rule;
using detail::Scope;
using detail::value_v;

k::Proposition Generator::available(const Scope& scope, std::size_t position) {
    const Hypothesis& hypothesis = scope.hypotheses[position];
    return k::shift(hypothesis.proposition, static_cast<std::uint32_t>(scope.locals.size() - hypothesis.binders));
}

k::ProofTerm Generator::use(const Scope& scope, std::size_t position) {
    return k::ProofTerm::hypothesis(
        k::HypothesisIndex{static_cast<std::uint32_t>(scope.hypotheses.size() - 1 - position)});
}

std::size_t Generator::suppose(Scope& scope, k::Proposition premise, bool usable) {
    scope.hypotheses.push_back(Hypothesis{std::move(premise), scope.locals.size(), usable});
    return scope.hypotheses.size() - 1;
}

Derivation Generator::discharge(Scope& scope, std::size_t mark, Derivation derived) {
    while (scope.hypotheses.size() > mark) {
        k::Proposition premise = std::move(scope.hypotheses.back().proposition);
        scope.hypotheses.pop_back();
        derived.proof = k::ProofTerm::implication_introduction(premise, std::move(derived.proof));
        derived.proposition = k::Proposition::implication(std::move(premise), std::move(derived.proposition));
    }
    return derived;
}

Derivation Generator::derive(Scope& scope, unsigned depth) {
    const auto rule = static_cast<Rule>(depth == 0 ? below(4) : below(kRuleCount));
    switch (rule) {
        case Rule::ReflexivitySame:
            return reflexivity(scope, 0);
        case Rule::ReflexivityEquivalent:
            return reflexivity(scope, 1);
        case Rule::ReflexivityRandom:
            return depth == 0 ? reflexivity(scope, 1) : reflexivity(scope, 2);
        case Rule::Hypothesis:
            return hypothesis(scope);
        case Rule::ForallIntroduction:
            return forall_introduction(scope, depth);
        case Rule::ImplicationIntroduction:
            return implication_introduction(scope, depth);
        case Rule::ForallElimination:
            return forall_elimination(scope, depth);
        case Rule::ImplicationElimination:
            return implication_elimination(scope, depth);
        case Rule::ConjunctionIntroduction:
            return conjunction_introduction(scope, depth);
        case Rule::ConjunctionElimination:
            return conjunction_elimination(scope, depth);
        case Rule::DisjunctionIntroduction:
            return disjunction_introduction(scope, depth);
        case Rule::DisjunctionElimination:
            return disjunction_elimination(scope, depth);
        case Rule::FalsityElimination:
            return falsity_elimination(scope, depth);
        case Rule::EqualityElimination:
            return equality_elimination(scope, depth);
        case Rule::ConditionalElimination:
            return conditional_elimination(scope, depth);
        case Rule::LinearArithmetic:
            return linear_arithmetic(scope);
        case Rule::UnsignedInduction:
            return unsigned_induction(scope);
    }
    return reflexivity(scope, 0);
}

Derivation Generator::unsigned_induction(Scope& scope) {
    static constexpr std::uint16_t widths[] = {1, 2, 3, 4};
    const k::IntType type{widths[below(4)], k::Signedness::Unsigned};
    const k::Type bound = as_type(type);
    const bool closed = scope.locals.empty() && scope.hypotheses.empty();
    const bool reflexive = !closed || one_in(3);
    scope.locals.push_back(bound);
    k::Proposition body = reflexive ? k::Proposition::equality(bound, k::Term::variable(k::VarIndex{0}),
                                                               k::Term::variable(k::VarIndex{0}))
                                    : comparison_proposition(scope.locals, 2);
    scope.locals.pop_back();
    const k::Proposition base_goal = k::induction_base(type, body);
    const k::Proposition step_goal = k::induction_step(type, body);
    k::ProofTerm base = k::ProofTerm::reflexivity();
    k::ProofTerm step = k::ProofTerm::reflexivity();
    if (reflexive) {
        // forall n. n < max -> P(n) -> P(n + 1), each P an instance of x = x.
        const auto& quantified = std::get<k::Forall>(step_goal.node);
        const auto& range = std::get<k::Implies>(quantified.body->node);
        const auto& hypothesis = std::get<k::Implies>(range.conclusion->node);
        step = k::ProofTerm::forall_introduction(
            bound, k::ProofTerm::implication_introduction(
                       *range.premise,
                       k::ProofTerm::implication_introduction(*hypothesis.premise, k::ProofTerm::reflexivity())));
    } else {
        if (auto proposed = automation::propose(context_, base_goal)) {
            base = std::move(proposed->proof);
        }
        if (auto proposed = automation::propose(context_, step_goal)) {
            step = std::move(proposed->proof);
        }
    }
    k::Type stated = bound;
    k::Proposition goal = k::Proposition::for_all(bound, body);
    if (perturb()) {
        switch (below(3)) {
            case 0:
                stated = as_type(k::IntType{widths[below(4)], k::Signedness::Unsigned});
                break;
            case 1:
                stated = as_type(k::IntType{type.width, k::Signedness::Signed});
                goal = k::Proposition::for_all(stated, body);
                break;
            default:
                std::swap(base, step);
                break;
        }
    }
    return Derivation{std::move(goal), k::ProofTerm::unsigned_induction(stated, std::move(base), std::move(step))};
}

Derivation Generator::misclaim(Scope& scope, Derivation derived) {
    derived.proposition = proposition(scope.locals, 1);
    return derived;
}

Derivation Generator::reflexivity(Scope& scope, unsigned kind) {
    if (one_in(8)) {
        const k::Type at = one_in(2) ? value_v() : indexed_a();
        if (auto subject = abstract_term(scope.locals, at, 1)) {
            return Derivation{k::Proposition::equality(at, *subject, *subject), k::ProofTerm::reflexivity()};
        }
    }
    const k::IntType type = any_integer();
    k::Term lhs = integer_term(scope.locals, type, 3);
    k::Term rhs = lhs;
    if (kind == 1) {
        rhs = equivalent(scope.locals, lhs, 3);
    } else if (kind == 2 || perturb()) {
        rhs = integer_term(scope.locals, type, 3);
    }
    if (one_in(2)) {
        std::swap(lhs, rhs);
    }
    return Derivation{k::Proposition::equality(as_type(type), std::move(lhs), std::move(rhs)),
                      k::ProofTerm::reflexivity()};
}

Derivation Generator::hypothesis(Scope& scope) {
    const auto position = find(scope, [](const k::Proposition&) { return true; });
    if (!position) {
        return reflexivity(scope, 0);
    }
    Derivation derived{available(scope, *position), use(scope, *position)};
    if (perturb()) {
        switch (below(3)) {
            case 0: {
                // A neighbouring index.
                auto& index = std::get<k::Hypothesis>(derived.proof.node).index.value;
                index = one_in(2) ? index + 1 : (index == 0 ? 0 : index - 1);
                break;
            }
            case 1:
                // The premise as it was written, not restated for here.
                derived.proposition = scope.hypotheses[*position].proposition;
                break;
            default:
                return misclaim(scope, std::move(derived));
        }
    }
    return derived;
}

Derivation Generator::forall_introduction(Scope& scope, unsigned depth) {
    const k::Type bound = binder();
    scope.locals.push_back(bound);
    Derivation body = derive(scope, depth - 1);
    scope.locals.pop_back();
    k::Type stated = bound;
    if (perturb()) {
        stated = binder();
    }
    return Derivation{k::Proposition::for_all(bound, std::move(body.proposition)),
                      k::ProofTerm::forall_introduction(stated, std::move(body.proof))};
}

Derivation Generator::implication_introduction(Scope& scope, unsigned depth) {
    k::Proposition premise = proposition(scope.locals, 2);
    const std::size_t mark = scope.hypotheses.size();
    suppose(scope, premise);
    Derivation body = derive(scope, depth - 1);
    Derivation derived = discharge(scope, mark, std::move(body));
    if (perturb()) {
        auto& introduction = std::get<k::ImplicationIntroduction>(derived.proof.node);
        introduction.premise = k::Box<k::Proposition>{proposition(scope.locals, 1)};
    }
    return derived;
}

std::optional<k::Term> Generator::argument(const Scope& scope, const k::Type& type) {
    return term(scope.locals, type, 2);
}

Derivation Generator::forall_elimination(Scope& scope, unsigned depth) {
    const std::size_t mark = scope.hypotheses.size();
    k::Proposition quantified = k::Proposition::falsity();
    k::ProofTerm evidence = k::ProofTerm::reflexivity();
    const auto existing =
        find(scope, [](const k::Proposition& p) { return std::holds_alternative<k::Forall>(p.node); });
    const std::uint32_t source = below(3);
    if (source == 0 && existing) {
        quantified = available(scope, *existing);
        evidence = use(scope, *existing);
    } else {
        k::Type bound = binder();
        if (!argument(scope, bound)) {
            bound = as_type(small_integer());
        }
        if (source == 1) {
            scope.locals.push_back(bound);
            auto body = proposition(scope.locals, 2);
            scope.locals.pop_back();
            quantified = k::Proposition::for_all(bound, std::move(body));
            const std::size_t position = suppose(scope, quantified);
            evidence = use(scope, position);
        } else {
            scope.locals.push_back(bound);
            Derivation body = derive(scope, depth - 1);
            scope.locals.pop_back();
            quantified = k::Proposition::for_all(bound, body.proposition);
            evidence = k::ProofTerm::forall_introduction(bound, std::move(body.proof));
        }
    }
    const auto& forall = std::get<k::Forall>(quantified.node);
    auto chosen = argument(scope, forall.binder);
    if (!chosen) {
        return discharge(scope, mark, reflexivity(scope, 0));
    }
    k::Proposition conclusion = k::instantiate(*forall.body, *chosen);
    k::Term given = *chosen;
    k::Proposition restated = quantified;
    if (perturb()) {
        switch (below(4)) {
            case 0:
                // An argument of some other type.
                given = literal(any_integer());
                break;
            case 1:
                // The conclusion at some other argument.
                if (auto other = argument(scope, forall.binder)) {
                    conclusion = k::instantiate(*forall.body, *other);
                }
                break;
            case 2:
                // A restatement that is not what the evidence establishes.
                restated = k::Proposition::for_all(forall.binder, [&] {
                    scope.locals.push_back(forall.binder);
                    auto body = proposition(scope.locals, 1);
                    scope.locals.pop_back();
                    return body;
                }());
                conclusion = k::instantiate(*std::get<k::Forall>(restated.node).body, *chosen);
                break;
            default:
                // Substitution without restating the argument for the
                // binders it descends through.
                conclusion = unshifted_instantiate(*forall.body, *chosen, 0);
                break;
        }
    }
    Derivation derived{std::move(conclusion),
                       k::ProofTerm::forall_elimination(std::move(restated), std::move(evidence), std::move(given))};
    return discharge(scope, mark, std::move(derived));
}

k::Term Generator::unshifted_instantiate(const k::Term& body, const k::Term& argument, std::uint32_t depth) {
    if (const auto* var = std::get_if<k::Var>(&body.node)) {
        if (var->index.value == depth) {
            return argument;
        }
        if (var->index.value > depth) {
            return k::Term::variable(k::VarIndex{var->index.value - 1});
        }
        return body;
    }
    return std::visit(
        [&](const auto& node) -> k::Term {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, k::Var> || std::is_same_v<Node, k::Literal>) {
                return k::Term{node};
            } else {
                Node copy = node;
                for (k::Term& child : copy.arguments) {
                    child = unshifted_instantiate(child, argument, depth);
                }
                return k::Term{std::move(copy)};
            }
        },
        body.node);
}

k::Proposition Generator::unshifted_instantiate(const k::Proposition& body, const k::Term& argument,
                                                std::uint32_t depth) {
    if (const auto* quantified = std::get_if<k::Forall>(&body.node)) {
        return k::Proposition::for_all(quantified->binder,
                                       unshifted_instantiate(*quantified->body, argument, depth + 1));
    }
    if (const auto* implication = std::get_if<k::Implies>(&body.node)) {
        return k::Proposition::implication(unshifted_instantiate(*implication->premise, argument, depth),
                                           unshifted_instantiate(*implication->conclusion, argument, depth));
    }
    if (const auto* conjunction = std::get_if<k::And>(&body.node)) {
        return k::Proposition::conjunction(unshifted_instantiate(*conjunction->left, argument, depth),
                                           unshifted_instantiate(*conjunction->right, argument, depth));
    }
    if (const auto* disjunction = std::get_if<k::Or>(&body.node)) {
        return k::Proposition::disjunction(unshifted_instantiate(*disjunction->left, argument, depth),
                                           unshifted_instantiate(*disjunction->right, argument, depth));
    }
    if (const auto* equality = std::get_if<k::Eq>(&body.node)) {
        return k::Proposition::equality(equality->type, unshifted_instantiate(equality->lhs, argument, depth),
                                        unshifted_instantiate(equality->rhs, argument, depth));
    }
    return body;
}

Derivation Generator::implication_elimination(Scope& scope, unsigned depth) {
    const std::size_t mark = scope.hypotheses.size();
    k::Proposition premise = k::Proposition::falsity();
    std::optional<std::size_t> premise_supposed;
    std::optional<k::ProofTerm> premise_evidence;
    if (one_in(3)) {
        premise = proposition(scope.locals, 2);
        premise_supposed = suppose(scope, premise);
    } else {
        Derivation derived = derive(scope, depth - 1);
        premise = std::move(derived.proposition);
        premise_evidence = std::move(derived.proof);
    }

    k::Proposition implication = k::Proposition::falsity();
    k::ProofTerm implication_evidence = k::ProofTerm::reflexivity();
    std::uint32_t pushed_after = 0;
    if (one_in(3)) {
        k::Proposition conclusion = proposition(scope.locals, 2);
        implication = k::Proposition::implication(premise, std::move(conclusion));
        const std::size_t position = suppose(scope, implication);
        implication_evidence = use(scope, position);
        pushed_after = 1;
    } else {
        const std::size_t inner = scope.hypotheses.size();
        suppose(scope, premise);
        Derivation body = derive(scope, depth - 1);
        Derivation introduced = discharge(scope, inner, std::move(body));
        implication = std::move(introduced.proposition);
        implication_evidence = std::move(introduced.proof);
    }

    k::ProofTerm evidence = premise_supposed ? use(scope, *premise_supposed) : lift(*premise_evidence, pushed_after);
    k::Proposition conclusion = *std::get<k::Implies>(implication.node).conclusion;
    k::Proposition restated = implication;
    if (perturb()) {
        if (one_in(2)) {
            // Evidence for some other premise.
            evidence = reflexivity(scope, 2).proof;
        } else {
            restated = k::Proposition::implication(proposition(scope.locals, 1), conclusion);
        }
    }
    Derivation derived{std::move(conclusion),
                       k::ProofTerm::implication_elimination(std::move(restated), std::move(implication_evidence),
                                                             std::move(evidence))};
    return discharge(scope, mark, std::move(derived));
}

Derivation Generator::premise(Scope& scope, unsigned depth) {
    if (one_in(3)) {
        k::Proposition supposed = proposition(scope.locals, 2);
        const std::size_t position = suppose(scope, supposed);
        return Derivation{std::move(supposed), use(scope, position)};
    }
    return derive(scope, depth - 1);
}

Derivation Generator::conjunction_of(Scope& scope, unsigned depth) {
    Derivation left = premise(scope, depth);
    const std::size_t between = scope.hypotheses.size();
    Derivation right = premise(scope, depth);
    const auto lifted = static_cast<std::uint32_t>(scope.hypotheses.size() - between);
    return Derivation{k::Proposition::conjunction(left.proposition, right.proposition),
                      k::ProofTerm::conjunction_introduction(lift(left.proof, lifted), std::move(right.proof))};
}

Derivation Generator::conjunction_introduction(Scope& scope, unsigned depth) {
    const std::size_t mark = scope.hypotheses.size();
    Derivation both = conjunction_of(scope, depth);
    if (perturb()) {
        const auto& sides = std::get<k::And>(both.proposition.node);
        both.proposition = k::Proposition::conjunction(*sides.left, proposition(scope.locals, 1));
    }
    return discharge(scope, mark, std::move(both));
}

Derivation Generator::conjunction_elimination(Scope& scope, unsigned depth) {
    const std::size_t mark = scope.hypotheses.size();
    k::Proposition conjunction = k::Proposition::falsity();
    k::ProofTerm evidence = k::ProofTerm::reflexivity();
    const auto existing = find(scope, [](const k::Proposition& p) { return std::holds_alternative<k::And>(p.node); });
    if (existing && one_in(2)) {
        conjunction = available(scope, *existing);
        evidence = use(scope, *existing);
    } else {
        Derivation both = conjunction_of(scope, depth);
        conjunction = std::move(both.proposition);
        evidence = std::move(both.proof);
    }
    const bool right = one_in(2);
    const auto& sides = std::get<k::And>(conjunction.node);
    k::Proposition conclusion = right ? *sides.right : *sides.left;
    bool stated_right = right;
    if (perturb()) {
        stated_right = !right;
    }
    Derivation derived{std::move(conclusion),
                       k::ProofTerm::conjunction_elimination(conjunction, std::move(evidence), stated_right)};
    return discharge(scope, mark, std::move(derived));
}

} // namespace cppl::testing::kernel_generator
