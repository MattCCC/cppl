#include "cppl/kernel/proposition.hpp"

#include <variant>

namespace cppl::kernel {

std::string describe(const Proposition& proposition) {
    if (const auto* quantified = std::get_if<Forall>(&proposition.node)) {
        return "forall " + describe(quantified->binder) + ". " + describe(*quantified->body);
    }
    if (const auto* implication = std::get_if<Implies>(&proposition.node)) {
        return "(" + describe(*implication->premise) + " -> " +
               describe(*implication->conclusion) + ")";
    }
    const auto& equality = std::get<Eq>(proposition.node);
    return "Eq<" + describe(equality.type) + ">(" + describe(equality.lhs) + ", " +
           describe(equality.rhs) + ")";
}

}  // namespace cppl::kernel
