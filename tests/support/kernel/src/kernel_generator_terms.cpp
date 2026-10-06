// The terms, propositions and definitions a generated derivation is about,
// and the definitional equivalence between them.

#include "cppl/kernel/box.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/kernel_model.hpp"
#include "kernel_generator_detail.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::testing::kernel_generator {

using detail::Generator;
using detail::indexed_a;
using detail::integer;
using detail::kBoolean;
using detail::value_v;
using detail::value_w;

k::Term Generator::literal(const k::IntType& type) {
    const Wide least = kernel_model::machine::lowest(type);
    const Wide greatest = kernel_model::machine::highest(type);
    Wide value = 0;
    switch (below(7)) {
        case 0:
            value = 0;
            break;
        case 1:
            value = 1;
            break;
        case 2:
            value = least;
            break;
        case 3:
            value = greatest;
            break;
        case 4:
            value = type.signedness == k::Signedness::Signed ? Wide{-1} : greatest - 1;
            break;
        case 5:
            value = Wide{below(5)} - 2;
            break;
        default:
            value = Wide{bits64()};
            break;
    }
    return k::Term::literal(type, kernel_model::machine::wrap(type, value));
}

std::vector<std::uint32_t> Generator::variables_of(std::span<const k::Type> locals, const k::Type& type) {
    std::vector<std::uint32_t> found;
    for (std::size_t position = 0; position < locals.size(); ++position) {
        if (locals[position] == type) {
            found.push_back(static_cast<std::uint32_t>(locals.size() - 1 - position));
        }
    }
    return found;
}

std::optional<k::Term> Generator::variable(std::span<const k::Type> locals, const k::Type& type) {
    const auto found = variables_of(locals, type);
    if (found.empty()) {
        return std::nullopt;
    }
    return k::Term::variable(k::VarIndex{found[below(static_cast<std::uint32_t>(found.size()))]});
}

std::optional<k::Term> Generator::abstract_term(std::span<const k::Type> locals, const k::Type& type, unsigned depth) {
    if (type == value_v() && depth > 0 && one_in(3)) {
        if (auto whole = variable(locals, value_w())) {
            return k::Term::project(value_w(), 0, std::move(*whole));
        }
    }
    if (depth > 0 && one_in(3)) {
        for (const k::Definition& definition : context_.definitions()) {
            if (definition.result == type) {
                if (auto call = call_of(locals, definition, depth - 1)) {
                    return call;
                }
            }
        }
    }
    return variable(locals, type);
}

std::optional<k::Term> Generator::call_of(std::span<const k::Type> locals, const k::Definition& definition,
                                          unsigned depth) {
    std::vector<k::Term> arguments;
    for (const k::Type& parameter : definition.parameters) {
        auto argument = term(locals, parameter, depth);
        if (!argument) {
            return std::nullopt;
        }
        arguments.push_back(std::move(*argument));
    }
    return k::Term::call(definition.id, std::move(arguments));
}

std::optional<k::Term> Generator::term(std::span<const k::Type> locals, const k::Type& type, unsigned depth) {
    if (type.is_integer()) {
        return integer_term(locals, type.integer_type(), depth);
    }
    return abstract_term(locals, type, depth);
}

k::Term Generator::integer_term(std::span<const k::Type> locals, const k::IntType& type, unsigned depth) {
    const bool boolean = type == kBoolean;
    const std::uint32_t choice = depth == 0 ? below(3) : below(boolean ? 15 : 12);
    const auto sub = [&](const k::IntType& at) {
        return integer_term(locals, at, depth - 1);
    };
    switch (choice) {
        case 1:
        case 2:
        case 9:
            if (auto found = variable(locals, as_type(type))) {
                return *found;
            }
            return literal(type);
        case 3: {
            static constexpr k::PrimOp ring[] = {k::PrimOp::AddWrap, k::PrimOp::SubWrap, k::PrimOp::MulWrap};
            const k::PrimOp op = ring[below(3)];
            // A product is kept linear most of the time, so arithmetic
            // can close what it states.
            if (op == k::PrimOp::MulWrap && !one_in(3)) {
                return k::Term::primitive(op, type, {literal(type), sub(type)});
            }
            return k::Term::primitive(op, type, {sub(type), sub(type)});
        }
        case 4:
            return k::Term::primitive(k::PrimOp::Select, type, {sub(kBoolean), sub(type), sub(type)});
        case 5: {
            const k::PrimOp op = one_in(2) ? k::PrimOp::Quotient : k::PrimOp::Remainder;
            return k::Term::primitive(op, type, {sub(type), one_in(2) ? literal(type) : sub(type)});
        }
        case 6:
            return k::Term::primitive(k::PrimOp::Convert, type, {sub(any_integer())});
        case 7: {
            for (const k::Definition& definition : context_.definitions()) {
                if (definition.result == as_type(type) && one_in(2)) {
                    if (auto call = call_of(locals, definition, depth - 1)) {
                        return *call;
                    }
                }
            }
            return literal(type);
        }
        case 8: {
            if (auto observed = observation(locals, type, depth)) {
                return *observed;
            }
            return literal(type);
        }
        case 10:
            if (depth > 0 && !boolean) {
                return k::Term::primitive(k::PrimOp::AddWrap, type, {sub(type), literal(type)});
            }
            return literal(type);
        case 11:
            if (depth > 0) {
                return k::Term::primitive(k::PrimOp::Convert, type, {sub(small_integer())});
            }
            return literal(type);
        case 12:
            return comparison(locals, depth);
        case 13: {
            static constexpr k::PrimOp fits[] = {k::PrimOp::AddFits, k::PrimOp::SubFits, k::PrimOp::MulFits};
            const k::IntType at = any_integer();
            return k::Term::primitive(fits[below(3)], at, {sub(at), sub(at)});
        }
        case 14:
            return k::Term::primitive(k::PrimOp::Not, kBoolean, {sub(kBoolean)});
        default:
            return literal(type);
    }
}

std::optional<k::Term> Generator::observation(std::span<const k::Type> locals, const k::IntType& type, unsigned depth) {
    const k::Type wanted = as_type(type);
    if (wanted == integer(4, k::Signedness::Signed)) {
        if (one_in(2)) {
            if (auto array = variable(locals, indexed_a())) {
                return k::Term::element(indexed_a(), std::move(*array),
                                        integer_term(locals, small_integer(), depth - 1));
            }
        }
        if (auto subject = abstract_term(locals, value_v(), depth - 1)) {
            return k::Term::project(value_v(), 0, std::move(*subject));
        }
    }
    if (wanted == integer(2, k::Signedness::Unsigned)) {
        if (auto subject = abstract_term(locals, value_v(), depth - 1)) {
            return k::Term::project(value_v(), 1, std::move(*subject));
        }
    }
    if (wanted == integer(8, k::Signedness::Signed)) {
        if (auto subject = variable(locals, value_w())) {
            return k::Term::project(value_w(), 1, std::move(*subject));
        }
    }
    return std::nullopt;
}

k::Term Generator::comparison(std::span<const k::Type> locals, unsigned depth) {
    static constexpr k::PrimOp orders[] = {k::PrimOp::Equal,     k::PrimOp::NotEqual, k::PrimOp::Less,
                                           k::PrimOp::LessEqual, k::PrimOp::Greater,  k::PrimOp::GreaterEqual};
    const k::IntType at = one_in(3) ? any_integer() : small_integer();
    const unsigned below_depth = depth == 0 ? 0 : depth - 1;
    return k::Term::primitive(orders[below(6)], at,
                              {integer_term(locals, at, below_depth), integer_term(locals, at, below_depth)});
}

k::Proposition Generator::comparison_proposition(std::span<const k::Type> locals, unsigned depth) {
    if (one_in(3)) {
        const k::IntType at = small_integer();
        return k::Proposition::equality(as_type(at), integer_term(locals, at, depth), integer_term(locals, at, depth));
    }
    return k::Proposition::equality(as_type(kBoolean), comparison(locals, depth),
                                    k::Term::literal(kBoolean, one_in(4) ? 0 : 1));
}

k::Proposition Generator::proposition(std::vector<k::Type>& locals, unsigned depth) {
    switch (depth == 0 ? below(2) : below(9)) {
        case 1: {
            const k::IntType at = any_integer();
            return k::Proposition::equality(as_type(at), integer_term(locals, at, 2), integer_term(locals, at, 2));
        }
        case 2: {
            const k::Type at = one_in(2) ? value_v() : indexed_a();
            auto lhs = abstract_term(locals, at, 1);
            auto rhs = abstract_term(locals, at, 1);
            if (lhs && rhs) {
                return k::Proposition::equality(at, std::move(*lhs), std::move(*rhs));
            }
            return comparison_proposition(locals, 2);
        }
        case 3: {
            const k::Type bound = binder();
            locals.push_back(bound);
            auto body = proposition(locals, depth - 1);
            locals.pop_back();
            return k::Proposition::for_all(bound, std::move(body));
        }
        case 4: {
            auto premise = proposition(locals, depth - 1);
            return k::Proposition::implication(std::move(premise), proposition(locals, depth - 1));
        }
        case 5: {
            auto left = proposition(locals, depth - 1);
            return k::Proposition::conjunction(std::move(left), proposition(locals, depth - 1));
        }
        case 6: {
            auto left = proposition(locals, depth - 1);
            return k::Proposition::disjunction(std::move(left), proposition(locals, depth - 1));
        }
        case 7:
            return k::Proposition::falsity();
        default:
            return comparison_proposition(locals, 2);
    }
}

void Generator::define_some() {
    const std::uint32_t count = below(4);
    for (std::uint32_t index = 0; index < count; ++index) {
        k::Definition definition;
        definition.id = k::DefId{index * 3 + 1};
        definition.name = "d" + std::to_string(index);
        const std::uint32_t parameters = below(4);
        for (std::uint32_t parameter = 0; parameter < parameters; ++parameter) {
            definition.parameters.push_back(one_in(4) ? (one_in(2) ? value_v() : indexed_a())
                                                      : as_type(small_integer()));
        }
        if (one_in(6)) {
            if (auto passed = variable(definition.parameters, value_v())) {
                definition.result = value_v();
                definition.body = *passed;
                admit(std::move(definition));
                continue;
            }
        }
        const k::IntType result = one_in(4) ? any_integer() : small_integer();
        definition.result = as_type(result);
        definition.body = integer_term(definition.parameters, result, 3);
        admit(std::move(definition));
    }
}

void Generator::admit(k::Definition definition) {
    [[maybe_unused]] const auto admitted = context_.define(std::move(definition));
}

std::optional<k::Type> Generator::type_of(std::span<const k::Type> locals, const k::Term& term) const {
    auto typed = k::type_of(context_, locals, term);
    if (!typed) {
        return std::nullopt;
    }
    return *typed;
}

k::Term Generator::substitute_parameters(const k::Term& body, const std::vector<k::Term>& arguments) {
    if (const auto* var = std::get_if<k::Var>(&body.node)) {
        if (var->index.value < arguments.size()) {
            return arguments[arguments.size() - 1 - var->index.value];
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
                for (k::Term& argument : copy.arguments) {
                    argument = substitute_parameters(argument, arguments);
                }
                return k::Term{std::move(copy)};
            }
        },
        body.node);
}

k::Term Generator::equivalent(std::span<const k::Type> locals, const k::Term& term, unsigned depth) {
    const auto typed = type_of(locals, term);
    if (!typed || !typed->is_integer()) {
        return term;
    }
    const k::IntType type = typed->integer_type();
    const auto zero = k::Term::literal(type, 0);
    if (depth > 0 && one_in(2)) {
        // Rewrite inside one argument instead.
        return std::visit(
            [&](const auto& node) -> k::Term {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, k::Var> || std::is_same_v<Node, k::Literal>) {
                    return k::Term{node};
                } else {
                    Node copy = node;
                    if (!copy.arguments.empty()) {
                        const auto at = below(static_cast<std::uint32_t>(copy.arguments.size()));
                        copy.arguments[at] = equivalent(locals, copy.arguments[at], depth - 1);
                    }
                    return k::Term{std::move(copy)};
                }
            },
            term.node);
    }
    if (const auto* call = std::get_if<k::Call>(&term.node)) {
        if (const k::Definition* definition = context_.lookup(call->callee)) {
            return substitute_parameters(definition->body, call->arguments);
        }
    }
    if (const auto* primitive = std::get_if<k::Prim>(&term.node)) {
        const auto& a = primitive->arguments;
        switch (primitive->op) {
            case k::PrimOp::AddWrap:
            case k::PrimOp::MulWrap:
                if (a.size() == 2 && one_in(2)) {
                    return k::Term::primitive(primitive->op, primitive->type, {a[1], a[0]});
                }
                break;
            case k::PrimOp::Less:
                if (a.size() == 2) {
                    return k::Term::primitive(k::PrimOp::Greater, primitive->type, {a[1], a[0]});
                }
                break;
            case k::PrimOp::LessEqual:
                if (a.size() == 2) {
                    return k::Term::primitive(k::PrimOp::Not, kBoolean,
                                              {k::Term::primitive(k::PrimOp::Less, primitive->type, {a[1], a[0]})});
                }
                break;
            case k::PrimOp::Equal:
                if (a.size() == 2) {
                    return k::Term::primitive(k::PrimOp::Equal, primitive->type, {a[1], a[0]});
                }
                break;
            case k::PrimOp::NotEqual:
                if (a.size() == 2) {
                    return k::Term::primitive(k::PrimOp::Not, kBoolean,
                                              {k::Term::primitive(k::PrimOp::Equal, primitive->type, a)});
                }
                break;
            case k::PrimOp::Select:
                if (a.size() == 3) {
                    return k::Term::primitive(k::PrimOp::Select, primitive->type,
                                              {k::Term::primitive(k::PrimOp::Not, kBoolean, {a[0]}), a[2], a[1]});
                }
                break;
            case k::PrimOp::AddFits:
            case k::PrimOp::MulFits:
                if (a.size() == 2) {
                    return k::Term::primitive(primitive->op, primitive->type, {a[1], a[0]});
                }
                break;
            default:
                break;
        }
    }
    if (type == kBoolean) {
        return k::Term::primitive(k::PrimOp::Not, kBoolean, {k::Term::primitive(k::PrimOp::Not, kBoolean, {term})});
    }
    switch (below(5)) {
        case 0:
            return k::Term::primitive(k::PrimOp::AddWrap, type, {term, zero});
        case 1:
            return k::Term::primitive(k::PrimOp::MulWrap, type, {k::Term::literal(type, 1), term});
        case 2: {
            const k::Term other = integer_term(locals, type, 1);
            return k::Term::primitive(k::PrimOp::SubWrap, type,
                                      {k::Term::primitive(k::PrimOp::AddWrap, type, {term, other}), other});
        }
        case 3:
            return k::Term::primitive(k::PrimOp::Quotient, type, {term, k::Term::literal(type, 1)});
        default:
            return k::Term::primitive(k::PrimOp::SubWrap, type, {term, zero});
    }
}

k::Term Generator::abstract(const k::Term& term, const k::Term& target, std::uint32_t hole) {
    if (term == target) {
        return k::Term::variable(k::VarIndex{hole});
    }
    return std::visit(
        [&](const auto& node) -> k::Term {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, k::Var> || std::is_same_v<Node, k::Literal>) {
                return k::Term{node};
            } else {
                Node copy = node;
                for (k::Term& argument : copy.arguments) {
                    argument = abstract(argument, target, hole);
                }
                return k::Term{std::move(copy)};
            }
        },
        term.node);
}

k::Proposition Generator::abstract(const k::Proposition& proposition, const k::Term& target, std::uint32_t depth) {
    if (const auto* quantified = std::get_if<k::Forall>(&proposition.node)) {
        return k::Proposition::for_all(quantified->binder, abstract(*quantified->body, k::shift(target, 1), depth + 1));
    }
    if (const auto* implication = std::get_if<k::Implies>(&proposition.node)) {
        return k::Proposition::implication(abstract(*implication->premise, target, depth),
                                           abstract(*implication->conclusion, target, depth));
    }
    if (const auto* conjunction = std::get_if<k::And>(&proposition.node)) {
        return k::Proposition::conjunction(abstract(*conjunction->left, target, depth),
                                           abstract(*conjunction->right, target, depth));
    }
    if (const auto* disjunction = std::get_if<k::Or>(&proposition.node)) {
        return k::Proposition::disjunction(abstract(*disjunction->left, target, depth),
                                           abstract(*disjunction->right, target, depth));
    }
    if (const auto* equality = std::get_if<k::Eq>(&proposition.node)) {
        return k::Proposition::equality(equality->type, abstract(equality->lhs, target, depth),
                                        abstract(equality->rhs, target, depth));
    }
    return proposition;
}

} // namespace cppl::testing::kernel_generator
