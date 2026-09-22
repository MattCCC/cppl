#include "cppl/kernel/substitution.hpp"

#include <utility>
#include <variant>
#include <vector>

namespace cppl::kernel {

Term shift(const Term& term, std::uint32_t amount, std::uint32_t cutoff) {
    if (amount == 0) {
        return term;
    }

    if (const auto* variable = std::get_if<Var>(&term.node)) {
        if (variable->index.value < cutoff) {
            return term;
        }
        return Term::variable(VarIndex{variable->index.value + amount});
    }

    if (const auto* call = std::get_if<Call>(&term.node)) {
        std::vector<Term> arguments;
        arguments.reserve(call->arguments.size());
        for (const Term& argument : call->arguments) {
            arguments.push_back(shift(argument, amount, cutoff));
        }
        return Term::call(call->callee, std::move(arguments));
    }

    if (const auto* primitive = std::get_if<Prim>(&term.node)) {
        std::vector<Term> arguments;
        arguments.reserve(primitive->arguments.size());
        for (const Term& argument : primitive->arguments) {
            arguments.push_back(shift(argument, amount, cutoff));
        }
        return Term::primitive(primitive->op, primitive->type, std::move(arguments));
    }

    if (const auto* projection = std::get_if<Projection>(&term.node)) {
        Projection result = *projection;
        for (auto& child : result.arguments)
            child = shift(child, amount, cutoff);
        return Term{std::move(result)};
    }

    if (const auto* element = std::get_if<Element>(&term.node)) {
        Element result = *element;
        for (auto& child : result.arguments)
            child = shift(child, amount, cutoff);
        return Term{std::move(result)};
    }

    return term; // a literal denotes the same value under any binder
}

Proposition shift(const Proposition& proposition, std::uint32_t amount, std::uint32_t cutoff) {
    if (amount == 0) {
        return proposition;
    }

    if (const auto* quantified = std::get_if<Forall>(&proposition.node)) {
        return Proposition::for_all(quantified->binder, shift(*quantified->body, amount, cutoff + 1));
    }

    if (const auto* implication = std::get_if<Implies>(&proposition.node)) {
        return Proposition::implication(shift(*implication->premise, amount, cutoff),
                                        shift(*implication->conclusion, amount, cutoff));
    }

    if (const auto* conjunction = std::get_if<And>(&proposition.node)) {
        return Proposition::conjunction(shift(*conjunction->left, amount, cutoff),
                                        shift(*conjunction->right, amount, cutoff));
    }

    if (const auto* disjunction = std::get_if<Or>(&proposition.node)) {
        return Proposition::disjunction(shift(*disjunction->left, amount, cutoff),
                                        shift(*disjunction->right, amount, cutoff));
    }

    const auto& equality = std::get<Eq>(proposition.node);
    return Proposition::equality(equality.type, shift(equality.lhs, amount, cutoff),
                                 shift(equality.rhs, amount, cutoff));
}

Term instantiate(const Term& body, const Term& argument, std::uint32_t depth) {
    if (const auto* variable = std::get_if<Var>(&body.node)) {
        if (variable->index.value == depth) {
            return shift(argument, depth, 0);
        }
        if (variable->index.value > depth) {
            return Term::variable(VarIndex{variable->index.value - 1});
        }
        return body;
    }

    if (const auto* call = std::get_if<Call>(&body.node)) {
        std::vector<Term> arguments;
        arguments.reserve(call->arguments.size());
        for (const Term& nested : call->arguments) {
            arguments.push_back(instantiate(nested, argument, depth));
        }
        return Term::call(call->callee, std::move(arguments));
    }

    if (const auto* primitive = std::get_if<Prim>(&body.node)) {
        std::vector<Term> arguments;
        arguments.reserve(primitive->arguments.size());
        for (const Term& nested : primitive->arguments) {
            arguments.push_back(instantiate(nested, argument, depth));
        }
        return Term::primitive(primitive->op, primitive->type, std::move(arguments));
    }

    if (const auto* projection = std::get_if<Projection>(&body.node)) {
        Projection result = *projection;
        for (auto& child : result.arguments)
            child = instantiate(child, argument, depth);
        return Term{std::move(result)};
    }

    if (const auto* element = std::get_if<Element>(&body.node)) {
        Element result = *element;
        for (auto& child : result.arguments)
            child = instantiate(child, argument, depth);
        return Term{std::move(result)};
    }

    return body;
}

Proposition instantiate(const Proposition& body, const Term& argument, std::uint32_t depth) {
    if (const auto* quantified = std::get_if<Forall>(&body.node)) {
        return Proposition::for_all(quantified->binder, instantiate(*quantified->body, argument, depth + 1));
    }

    if (const auto* implication = std::get_if<Implies>(&body.node)) {
        return Proposition::implication(instantiate(*implication->premise, argument, depth),
                                        instantiate(*implication->conclusion, argument, depth));
    }

    if (const auto* conjunction = std::get_if<And>(&body.node)) {
        return Proposition::conjunction(instantiate(*conjunction->left, argument, depth),
                                        instantiate(*conjunction->right, argument, depth));
    }

    if (const auto* disjunction = std::get_if<Or>(&body.node)) {
        return Proposition::disjunction(instantiate(*disjunction->left, argument, depth),
                                        instantiate(*disjunction->right, argument, depth));
    }

    const auto& equality = std::get<Eq>(body.node);
    return Proposition::equality(equality.type, instantiate(equality.lhs, argument, depth),
                                 instantiate(equality.rhs, argument, depth));
}

} // namespace cppl::kernel
