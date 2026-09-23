#include "cppl/kernel/proposition.hpp"

#include <variant>

namespace cppl::kernel {

Proposition predicate(const Term& condition, bool positive) {
    const Term* term = &condition;
    while (const auto* primitive = std::get_if<Prim>(&term->node)) {
        if (primitive->op != PrimOp::Not || primitive->arguments.size() != 1)
            break;
        positive = !positive;
        term = &primitive->arguments[0];
    }
    if (const auto* primitive = std::get_if<Prim>(&term->node);
        primitive != nullptr && primitive->arguments.size() == 2 &&
        (primitive->op == PrimOp::Equal || primitive->op == PrimOp::NotEqual)) {
        if (primitive->op == PrimOp::NotEqual)
            positive = !positive;
        if (positive) {
            return Proposition::equality(Type{primitive->type}, primitive->arguments[0], primitive->arguments[1]);
        }
        return Proposition::equality(Type{kBoolean},
                                     Term::primitive(PrimOp::Equal, primitive->type, primitive->arguments),
                                     Term::literal(kBoolean, 0));
    }
    return Proposition::equality(Type{kBoolean}, *term, Term::literal(kBoolean, positive ? 1 : 0));
}

std::string describe(const Proposition& proposition) {
    if (const auto* quantified = std::get_if<Forall>(&proposition.node)) {
        return "forall " + describe(quantified->binder) + ". " + describe(*quantified->body);
    }
    if (const auto* implication = std::get_if<Implies>(&proposition.node)) {
        return "(" + describe(*implication->premise) + " -> " + describe(*implication->conclusion) + ")";
    }
    if (const auto* conjunction = std::get_if<And>(&proposition.node)) {
        return "(" + describe(*conjunction->left) + " && " + describe(*conjunction->right) + ")";
    }
    if (const auto* disjunction = std::get_if<Or>(&proposition.node)) {
        return "(" + describe(*disjunction->left) + " || " + describe(*disjunction->right) + ")";
    }
    if (std::holds_alternative<Falsity>(proposition.node)) {
        return "False";
    }
    const auto& equality = std::get<Eq>(proposition.node);
    return "Eq<" + describe(equality.type) + ">(" + describe(equality.lhs) + ", " + describe(equality.rhs) + ")";
}

} // namespace cppl::kernel
