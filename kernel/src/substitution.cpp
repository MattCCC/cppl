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

    return term;  // a literal denotes the same value under any binder
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

    return body;
}

Proposition instantiate(const Proposition& body, const Term& argument, std::uint32_t depth) {
    if (const auto* quantified = std::get_if<Forall>(&body.node)) {
        return Proposition::for_all(quantified->binder,
                                    instantiate(*quantified->body, argument, depth + 1));
    }

    const auto& equality = std::get<Eq>(body.node);
    return Proposition::equality(equality.type, instantiate(equality.lhs, argument, depth),
                                 instantiate(equality.rhs, argument, depth));
}

}  // namespace cppl::kernel
