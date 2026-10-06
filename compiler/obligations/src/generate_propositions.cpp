// Lowering VIR propositions into core propositions, with the memberships
// their refined quantified variables carry.

#include "cppl/kernel/proposition.hpp"
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
#include <expected>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations {

using detail::generation::fail;
using detail::generation::TermLowering;

namespace {

using detail::Failure;

using detail::DefinitionMap;

// Lowers a specification expression into a core proposition.
//
// A C++ equality between built-in integer values denotes propositional equality
// of those values. The correspondence holds for this operand type only, and is
// established here rather than assumed anywhere else (SPEC.md 7.3).
// Equivalence duplicates both operands in its derived core representation.
// Bound that expansion before allocating it; source nesting alone is not a
// useful bound for repeated equivalences.
bool fits_proposition(const vir::Expr& expression, std::size_t copies, std::size_t& remaining, unsigned depth = 0) {
    if (depth > 128 || copies > remaining)
        return false;
    remaining -= copies;
    return std::visit(
        [&](const auto& node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, vir::Connective>) {
                if (node.kind == vir::Connective::Kind::Equivalence) {
                    if (copies > remaining / 2)
                        return false;
                    remaining -= 2 * copies; // And plus two Implies nodes
                    copies *= 2;
                }
            }
            if constexpr (requires { node.operands; }) {
                for (const auto& operand : node.operands)
                    if (!fits_proposition(operand, copies, remaining, depth + 1))
                        return false;
            } else if constexpr (requires { node.arguments; }) {
                for (const auto& argument : node.arguments)
                    if (!fits_proposition(argument, copies, remaining, depth + 1))
                        return false;
            } else if constexpr (std::is_same_v<Node, vir::Universal>) {
                if (node.binders.size() > remaining / copies)
                    return false;
                remaining -= node.binders.size() * copies;
                for (const auto& body : node.body)
                    if (!fits_proposition(body, copies, remaining, depth + 1))
                        return false;
            }
            return true;
        },
        expression.node);
}

// Lowers the terms of a specification one at a time, each afresh, so reading a
// subterm again for its definedness conditions draws on no budget the term
// itself already spent.
detail::TermLowerer specification_terms(const DefinitionMap& definitions, std::size_t parameter_count) {
    return [&definitions, parameter_count](const vir::Expr& term) {
        TermLowering lowering(definitions, parameter_count);
        return lowering.lower(term);
    };
}

// Supposes of each binder of a group, outermost first, that it is a value of its
// type (SPEC.md FORALL-001, 8.1). A binder of a refinement type ranges over the
// values of its base type that satisfy the refinement's predicate, so that
// predicate, stated of the binder, is a premise of `body`. Every premise follows
// the whole group, so the group is instantiated exactly as an unrefined one is,
// and what an instantiation then states is the membership its terms owe before
// the body can be used. A binder whose type names no refinement adds nothing,
// so a proposition over such binders is the one stated without this.
// On failure `unstated`, when given, names the binder whose membership could
// not be stated.
std::expected<kernel::Proposition, Failure> suppose_membership(const Program& program,
                                                               const std::vector<vir::Type>& binders,
                                                               kernel::Proposition body,
                                                               const source::SourceLocation& location,
                                                               const vir::Type** unstated = nullptr) {
    for (std::size_t index = binders.size(); index > 0; --index) {
        const vir::Type& binder = binders[index - 1];
        const auto member = detail::refinement_membership(
            program, binder, kernel::Term::variable(kernel::parameter_reference(binders.size(), index - 1)));
        if (!member) {
            if (unstated != nullptr)
                *unstated = &binder;
            return fail("a binder of type '" + vir::describe(binder) +
                            "' has no stated range of values: " + member.error().reason,
                        location);
        }
        if (member->has_value())
            body = kernel::Proposition::implication(**member, std::move(body));
    }
    return body;
}

// Whether a value of `type` must satisfy a refinement: the type's own, or a
// component's at any depth (SPEC.md 17.6). A type nested past the bound is
// counted as one, so nothing deeper goes unasked.
bool carries_refinement(const vir::Type& type, unsigned depth = 0) {
    if (type.is_refined() || depth > 32)
        return true;
    if (!type.is_value())
        return false;
    return std::ranges::any_of(std::get<vir::ValueType>(type.node).projections, [depth](const vir::Type& component) {
        return carries_refinement(component, depth + 1);
    });
}

} // namespace

namespace detail::generation {

std::expected<kernel::Proposition, Failure> lower_proposition(const vir::Expr& expression, const Program& program,
                                                              const DefinitionMap& definitions,
                                                              std::size_t parameter_count) {
    const source::SourceLocation& location = expression.provenance.range.begin;
    std::size_t expansion_budget = 16384;
    if (!fits_proposition(expression, 1, expansion_budget))
        return fail("logical proposition expansion exceeds the supported limit", location);

    if (const auto* quantified = std::get_if<vir::Universal>(&expression.node)) {
        if (!expression.type.is_proposition() || quantified->binders.empty() || quantified->body.size() != 1)
            return fail("malformed universal proposition", location);
        auto body =
            lower_proposition(quantified->body[0], program, definitions, parameter_count + quantified->binders.size());
        if (!body)
            return body;
        body = suppose_membership(program, quantified->binders, std::move(*body), location);
        if (!body)
            return body;
        for (const auto& binder : std::views::reverse(quantified->binders)) {
            const auto type = lower_type(binder);
            if (!type)
                return fail("unsupported quantifier binder type", location);
            *body = kernel::Proposition::for_all(*type, std::move(*body));
        }
        return body;
    }
    // C++ `&&` between Boolean operands states the conjunction of what those
    // operands state (SPEC.md 7.6). Each operand is a
    // side-effect-free specification expression, so short-circuiting changes
    // which of them C++ evaluates and never what the statement means.
    // `||` states the disjunction of its operands for the same reason (SPEC.md
    // 7.8): both are pure specification expressions, so which of them C++ would
    // evaluate does not enter into what the proposition says.
    if (const auto* binary = std::get_if<vir::Binary>(&expression.node);
        binary != nullptr && (binary->op == vir::BinaryOp::And || binary->op == vir::BinaryOp::Or)) {
        const bool conjunction = binary->op == vir::BinaryOp::And;
        if (!expression.type.is_boolean() || binary->operands.size() != 2 || !binary->operands[0].type.is_boolean() ||
            !binary->operands[1].type.is_boolean())
            return fail(conjunction ? "malformed conjunction" : "malformed disjunction", location);
        auto left = lower_proposition(binary->operands[0], program, definitions, parameter_count);
        if (!left)
            return left;
        auto right = lower_proposition(binary->operands[1], program, definitions, parameter_count);
        if (!right)
            return right;
        return conjunction ? kernel::Proposition::conjunction(std::move(*left), std::move(*right))
                           : kernel::Proposition::disjunction(std::move(*left), std::move(*right));
    }

    if (const auto* connective = std::get_if<vir::Connective>(&expression.node)) {
        if (!expression.type.is_proposition() || connective->operands.size() != 2)
            return fail("malformed logical connective", location);
        auto left = lower_proposition(connective->operands[0], program, definitions, parameter_count);
        if (!left)
            return left;
        auto right = lower_proposition(connective->operands[1], program, definitions, parameter_count);
        if (!right)
            return right;
        switch (connective->kind) {
            case vir::Connective::Kind::Conjunction:
                return kernel::Proposition::conjunction(std::move(*left), std::move(*right));
            case vir::Connective::Kind::Disjunction:
                return kernel::Proposition::disjunction(std::move(*left), std::move(*right));
            case vir::Connective::Kind::Equivalence:
                return kernel::Proposition::conjunction(kernel::Proposition::implication(*left, *right),
                                                        kernel::Proposition::implication(*right, *left));
        }
        return fail("unknown logical connective", location);
    }

    if (const auto* implication = std::get_if<vir::Implication>(&expression.node)) {
        if (!expression.type.is_proposition() || implication->operands.size() != 2)
            return fail("malformed implication proposition", location);
        auto premise = lower_proposition(implication->operands[0], program, definitions, parameter_count);
        if (!premise)
            return premise;
        auto conclusion = lower_proposition(implication->operands[1], program, definitions, parameter_count);
        if (!conclusion)
            return conclusion;
        return kernel::Proposition::implication(std::move(*premise), std::move(*conclusion));
    }

    if (const auto* equality = std::get_if<vir::FormalEquality>(&expression.node)) {
        const auto type = lower_type(equality->operand_type);
        if (!expression.type.is_proposition() || !type || equality->operands.size() != 2 ||
            !(equality->operands[0].type == equality->operand_type) ||
            !(equality->operands[1].type == equality->operand_type)) {
            return fail("malformed formal equality", location);
        }
        // `Eq<R>(a, b)` equates two values of `R`. Its operands are values of
        // the base type, and nothing here shows them members of `R`, so the
        // equality would hold of values outside the type it names (SPEC.md
        // FORALL-001, REFINE-003). It is refused rather than stated at the base
        // type.
        if (carries_refinement(equality->operand_type)) {
            const vir::Type& named = equality->operand_type;
            return fail("formal equality at '" +
                            (named.is_refined() ? named.refinements.front().name : vir::spelled(named)) +
                            "' equates values of a refinement type, and its operands are not shown to be values of "
                            "it; state the equality at its base type",
                        location);
        }
        TermLowering lowering(definitions, parameter_count);
        auto lhs = lowering.lower(equality->operands[0]);
        if (!lhs)
            return std::unexpected(lhs.error());
        auto rhs = lowering.lower(equality->operands[1]);
        if (!rhs)
            return std::unexpected(rhs.error());
        return detail::specified(expression, kernel::Proposition::equality(*type, std::move(*lhs), std::move(*rhs)),
                                 specification_terms(definitions, parameter_count));
    }

    // A C++ condition states that it evaluates to true, and that every
    // operation it would evaluate on the way is defined: undefined behavior
    // never gives a specification its meaning (SPEC.md ARITH-010,
    // ADMISSIBLE-005). A condition with no such operation states its truth
    // alone, exactly as before.
    if (!expression.type.is_boolean())
        return fail("it does not state a comparison", location);
    return detail::specified(expression, specification_terms(definitions, parameter_count));
}

// Closes a proposition over a declaration's parameters, outermost first. A
// parameter is quantified as a binder is (SPEC.md 8), so a refined parameter
// ranges over the values of its type and its membership is supposed of it
// before the declaration's own premise.
std::optional<kernel::Proposition> quantify_over(const Program& program, const std::vector<vir::Parameter>& parameters,
                                                 kernel::Proposition body, std::string& unrepresented) {
    std::vector<vir::Type> types;
    types.reserve(parameters.size());
    for (const auto& parameter : parameters)
        types.push_back(parameter.type);
    const vir::Type* unstated = nullptr;
    auto ranged = suppose_membership(program, types, std::move(body), {}, &unstated);
    if (!ranged) {
        unrepresented = unstated != nullptr ? vir::describe(*unstated) : ranged.error().reason;
        return std::nullopt;
    }
    body = std::move(*ranged);
    for (const auto& parameter : std::views::reverse(parameters)) {
        const std::optional<kernel::Type> binder = lower_type(parameter.type);
        if (!binder.has_value()) {
            unrepresented = vir::describe(parameter.type);
            return std::nullopt;
        }
        body = kernel::Proposition::for_all(*binder, std::move(body));
    }
    return body;
}

} // namespace detail::generation

} // namespace cppl::obligations
