// Derivations by disjunction, falsity, equality and conditional elimination.

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
#include <utility>
#include <variant>
#include <vector>

namespace cppl::testing::kernel_generator {

using detail::Derivation;
using detail::Generator;
using detail::kBoolean;
using detail::Scope;
using detail::value_v;

Derivation Generator::disjunction_of(Scope& scope, unsigned depth) {
    Derivation side = premise(scope, depth);
    k::Proposition other = proposition(scope.locals, 2);
    const bool right = one_in(2);
    k::Proposition conclusion = right ? k::Proposition::disjunction(std::move(other), side.proposition)
                                      : k::Proposition::disjunction(side.proposition, std::move(other));
    return Derivation{std::move(conclusion), k::ProofTerm::disjunction_introduction(std::move(side.proof), right)};
}

Derivation Generator::disjunction_introduction(Scope& scope, unsigned depth) {
    const std::size_t mark = scope.hypotheses.size();
    Derivation one = disjunction_of(scope, depth);
    if (perturb()) {
        auto& introduction = std::get<k::DisjunctionIntroduction>(one.proof.node);
        introduction.right = !introduction.right;
    }
    return discharge(scope, mark, std::move(one));
}

Derivation Generator::disjunction_elimination(Scope& scope, unsigned depth) {
    const std::size_t mark = scope.hypotheses.size();
    k::Proposition disjunction = k::Proposition::falsity();
    k::ProofTerm evidence = k::ProofTerm::reflexivity();
    const auto existing = find(scope, [](const k::Proposition& p) { return std::holds_alternative<k::Or>(p.node); });
    if (existing && one_in(2)) {
        disjunction = available(scope, *existing);
        evidence = use(scope, *existing);
    } else if (one_in(2)) {
        disjunction = k::Proposition::disjunction(proposition(scope.locals, 2), proposition(scope.locals, 2));
        const std::size_t position = suppose(scope, disjunction);
        evidence = use(scope, position);
    } else {
        Derivation one = disjunction_of(scope, depth);
        disjunction = std::move(one.proposition);
        evidence = std::move(one.proof);
    }
    const auto& sides = std::get<k::Or>(disjunction.node);
    const k::Proposition& left = *sides.left;
    const k::Proposition& right = *sides.right;

    k::Proposition conclusion = k::Proposition::falsity();
    k::ProofTerm left_case = k::ProofTerm::reflexivity();
    k::ProofTerm right_case = k::ProofTerm::reflexivity();
    std::uint32_t lifted = 0;
    switch (below(3)) {
        case 0: {
            // The disjunction with its sides exchanged.
            conclusion = k::Proposition::disjunction(right, left);
            left_case = k::ProofTerm::implication_introduction(
                left, k::ProofTerm::disjunction_introduction(k::ProofTerm::hypothesis(k::HypothesisIndex{0}), true));
            right_case = k::ProofTerm::implication_introduction(
                right, k::ProofTerm::disjunction_introduction(k::ProofTerm::hypothesis(k::HypothesisIndex{0}), false));
            break;
        }
        case 1: {
            // A goal that follows from neither side in particular.
            const std::size_t placeholder = suppose(scope, k::Proposition::falsity(), false);
            Derivation body = derive(scope, depth - 1);
            scope.hypotheses.erase(scope.hypotheses.begin() + static_cast<std::ptrdiff_t>(placeholder));
            conclusion = body.proposition;
            left_case = k::ProofTerm::implication_introduction(left, body.proof);
            right_case = k::ProofTerm::implication_introduction(right, std::move(body.proof));
            break;
        }
        default: {
            // Each case supposed.
            conclusion = proposition(scope.locals, 2);
            const std::size_t from_left = suppose(scope, k::Proposition::implication(left, conclusion));
            const std::size_t from_right = suppose(scope, k::Proposition::implication(right, conclusion));
            left_case = use(scope, from_left);
            right_case = use(scope, from_right);
            lifted = 2;
            break;
        }
    }
    if (perturb()) {
        std::swap(left_case, right_case);
    }
    Derivation derived{std::move(conclusion),
                       k::ProofTerm::disjunction_elimination(disjunction, lift(evidence, lifted), std::move(left_case),
                                                             std::move(right_case))};
    return discharge(scope, mark, std::move(derived));
}

std::optional<k::ProofTerm> Generator::contradiction(Scope& scope) {
    if (const auto supposed =
            find(scope, [](const k::Proposition& p) { return std::holds_alternative<k::Falsity>(p.node); });
        supposed && one_in(2)) {
        return use(scope, *supposed);
    }
    return arithmetic_from_scope(scope, k::Proposition::falsity());
}

Derivation Generator::falsity_elimination(Scope& scope, unsigned depth) {
    const std::size_t mark = scope.hypotheses.size();
    std::optional<k::ProofTerm> evidence = contradiction(scope);
    if (!evidence) {
        if (one_in(2)) {
            const std::size_t position = suppose(scope, k::Proposition::falsity());
            evidence = use(scope, position);
        } else {
            // Two comparisons that cannot both hold, supposed.
            const k::IntType at = small_integer();
            const k::Term subject = integer_term(scope.locals, at, 1);
            const k::Term bound = literal(at);
            suppose(scope, k::Proposition::equality(as_type(kBoolean),
                                                    k::Term::primitive(k::PrimOp::Less, at, {subject, bound}),
                                                    k::Term::literal(kBoolean, 1)));
            suppose(scope,
                    k::Proposition::equality(
                        as_type(kBoolean),
                        k::Term::primitive(one_in(4) ? k::PrimOp::Less : k::PrimOp::GreaterEqual, at, {subject, bound}),
                        k::Term::literal(kBoolean, 1)));
            evidence = arithmetic_from_scope(scope, k::Proposition::falsity());
            if (!evidence) {
                evidence = derive(scope, depth - 1).proof;
            }
        }
    }
    k::ProofTerm stated = std::move(*evidence);
    if (perturb()) {
        stated = derive(scope, depth - 1).proof;
    }
    Derivation derived{proposition(scope.locals, 2), k::ProofTerm::falsity_elimination(std::move(stated))};
    return discharge(scope, mark, std::move(derived));
}

Derivation Generator::equality_elimination(Scope& scope, unsigned depth) {
    const std::size_t mark = scope.hypotheses.size();
    k::Type type = as_type(small_integer());
    k::Term lhs = k::Term::literal(k::IntType{1, k::Signedness::Unsigned}, 0);
    k::Term rhs = lhs;
    k::ProofTerm equality = k::ProofTerm::reflexivity();
    const auto existing = find(scope, [](const k::Proposition& p) { return std::holds_alternative<k::Eq>(p.node); });
    const std::uint32_t source = below(3);
    if (source == 0 && existing) {
        const k::Proposition stated = available(scope, *existing);
        const auto& eq = std::get<k::Eq>(stated.node);
        type = eq.type;
        lhs = eq.lhs;
        rhs = eq.rhs;
        equality = use(scope, *existing);
    } else if (source == 1) {
        type = one_in(4) ? value_v() : as_type(any_integer());
        auto a = term(scope.locals, type, 2);
        auto b = term(scope.locals, type, 2);
        if (!a || !b) {
            type = as_type(small_integer());
            a = integer_term(scope.locals, type.integer_type(), 2);
            b = integer_term(scope.locals, type.integer_type(), 2);
        }
        lhs = std::move(*a);
        rhs = std::move(*b);
        const std::size_t position = suppose(scope, k::Proposition::equality(type, lhs, rhs));
        equality = use(scope, position);
    } else {
        const k::IntType at = any_integer();
        type = as_type(at);
        lhs = integer_term(scope.locals, at, 2);
        rhs = equivalent(scope.locals, lhs, 2);
    }

    std::vector<k::Type> under = scope.locals;
    under.push_back(type);
    k::Proposition motive = k::Proposition::falsity();
    k::ProofTerm evidence = k::ProofTerm::reflexivity();
    std::uint32_t lifted_equality = 0;
    switch (below(4)) {
        case 0: {
            // Congruence: f(hole) = f(rhs).
            const k::IntType at = any_integer();
            const k::Term shape = integer_term(under, at, 1 + depth % 3);
            motive = k::Proposition::equality(as_type(at), shape, k::shift(k::instantiate(shape, rhs), 1));
            break;
        }
        case 1:
            // Symmetry: rhs = hole.
            motive = k::Proposition::equality(type, k::shift(rhs, 1), k::Term::variable(k::VarIndex{0}));
            break;
        case 2: {
            // A premise in scope, with rhs abstracted.
            const auto position = find(scope, [](const k::Proposition&) { return true; });
            if (position) {
                motive = abstract(k::shift(available(scope, *position), 1), k::shift(rhs, 1), 0);
                if (k::instantiate(motive, rhs) == available(scope, *position)) {
                    evidence = use(scope, *position);
                    break;
                }
            }
            [[fallthrough]];
        }
        default: {
            // Supposed at rhs.
            motive = proposition(under, 2);
            const std::size_t position = suppose(scope, k::instantiate(motive, rhs));
            evidence = use(scope, position);
            lifted_equality = 1;
            break;
        }
    }
    k::Proposition conclusion = k::instantiate(motive, lhs);
    k::Term stated_lhs = lhs;
    k::Term stated_rhs = rhs;
    k::Type stated_type = type;
    if (perturb()) {
        switch (below(4)) {
            case 0:
                // Transported the wrong way.
                std::swap(stated_lhs, stated_rhs);
                break;
            case 1:
                conclusion = k::instantiate(motive, rhs);
                std::swap(stated_lhs, stated_rhs);
                break;
            case 2:
                stated_type = as_type(any_integer());
                break;
            default:
                return misclaim(scope, discharge(scope, mark,
                                                 Derivation{std::move(conclusion),
                                                            k::ProofTerm::equality_elimination(
                                                                type, lhs, rhs, motive, lift(equality, lifted_equality),
                                                                evidence)}));
        }
    }
    Derivation derived{std::move(conclusion),
                       k::ProofTerm::equality_elimination(std::move(stated_type), std::move(stated_lhs),
                                                          std::move(stated_rhs), std::move(motive),
                                                          lift(equality, lifted_equality), std::move(evidence))};
    return discharge(scope, mark, std::move(derived));
}

Derivation Generator::conditional_elimination(Scope& scope, unsigned depth) {
    const std::size_t mark = scope.hypotheses.size();
    const k::IntType at = small_integer();
    const k::Type type = as_type(at);
    const k::Term condition = integer_term(scope.locals, kBoolean, 2);
    const k::Term when_true = integer_term(scope.locals, at, 2);
    const k::Term when_false = integer_term(scope.locals, at, 2);
    const k::Term selected = k::Term::primitive(k::PrimOp::Select, at, {condition, when_true, when_false});

    std::vector<k::Type> under = scope.locals;
    under.push_back(type);
    k::Proposition motive = k::Proposition::falsity();
    k::ProofTerm true_case = k::ProofTerm::reflexivity();
    k::ProofTerm false_case = k::ProofTerm::reflexivity();
    const k::Proposition if_true = k::predicate(condition, true);
    const k::Proposition if_false = k::predicate(condition, false);
    switch (below(3)) {
        case 0: {
            // Both cases supposed.
            motive = one_in(2) ? proposition(under, 2)
                               : k::Proposition::equality(as_type(at), k::Term::variable(k::VarIndex{0}),
                                                          integer_term(under, at, 2));
            const bool exchanged = perturb();
            const std::size_t first = suppose(
                scope, k::Proposition::implication(exchanged ? if_false : if_true, k::instantiate(motive, when_true)));
            const std::size_t second = suppose(
                scope, k::Proposition::implication(exchanged ? if_true : if_false, k::instantiate(motive, when_false)));
            true_case = use(scope, first);
            false_case = use(scope, second);
            break;
        }
        case 1: {
            // The selection restated in each case, from the premise alone.
            motive = k::Proposition::equality(type, k::Term::variable(k::VarIndex{0}), k::shift(selected, 1));
            true_case = restate(scope, if_true, k::instantiate(motive, when_true));
            false_case = restate(scope, if_false, k::instantiate(motive, when_false));
            break;
        }
        default: {
            const k::IntType other = any_integer();
            const k::Term shape = integer_term(under, other, 2);
            motive = k::Proposition::equality(as_type(other), shape, shape);
            true_case = k::ProofTerm::implication_introduction(if_true, k::ProofTerm::reflexivity());
            false_case = k::ProofTerm::implication_introduction(if_false, k::ProofTerm::reflexivity());
            (void)depth;
            break;
        }
    }
    k::Proposition conclusion = k::instantiate(motive, selected);
    k::Term stated_true = when_true;
    k::Term stated_false = when_false;
    if (perturb()) {
        switch (below(3)) {
            case 0:
                std::swap(stated_true, stated_false);
                conclusion = k::instantiate(
                    motive, k::Term::primitive(k::PrimOp::Select, at, {condition, stated_true, stated_false}));
                break;
            case 1:
                std::swap(true_case, false_case);
                break;
            default:
                conclusion = k::instantiate(
                    motive, k::Term::primitive(k::PrimOp::Select, at, {condition, when_false, when_true}));
                break;
        }
    }
    Derivation derived{std::move(conclusion), k::ProofTerm::conditional_elimination(
                                                  type, condition, std::move(stated_true), std::move(stated_false),
                                                  std::move(motive), std::move(true_case), std::move(false_case))};
    return discharge(scope, mark, std::move(derived));
}

} // namespace cppl::testing::kernel_generator
