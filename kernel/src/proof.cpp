#include "cppl/kernel/proof.hpp"

#include <variant>

namespace cppl::kernel {

std::string describe(const ProofTerm& proof) {
    if (const auto* arithmetic = std::get_if<LinearArithmetic>(&proof.node)) {
        std::string text = "linear_arithmetic(";
        for (std::size_t index = 0; index < arithmetic->facts.size(); ++index) {
            if (index != 0) {
                text += ", ";
            }
            text += describe(*arithmetic->facts[index].evidence);
        }
        return text + ")";
    }
    if (const auto* branch = std::get_if<ConditionalElimination>(&proof.node)) {
        return "conditional_elim(" + describe(branch->condition) + ", " + describe(*branch->true_case) + ", " +
               describe(*branch->false_case) + ")";
    }
    if (const auto* introduction = std::get_if<ForallIntroduction>(&proof.node)) {
        return "forall_intro(" + describe(introduction->binder) + ", " + describe(*introduction->body) + ")";
    }
    if (const auto* elimination = std::get_if<ForallElimination>(&proof.node)) {
        return "forall_elim(" + describe(*elimination->evidence) + ", " + describe(elimination->argument) + ")";
    }
    if (const auto* assumed = std::get_if<Hypothesis>(&proof.node)) {
        return "hypothesis(" + std::to_string(assumed->index.value) + ")";
    }
    if (const auto* introduction = std::get_if<ImplicationIntroduction>(&proof.node)) {
        return "implies_intro(" + describe(*introduction->premise) + ", " + describe(*introduction->body) + ")";
    }
    if (const auto* application = std::get_if<ImplicationElimination>(&proof.node)) {
        return "implies_elim(" + describe(*application->evidence) + ", " + describe(*application->premise) + ")";
    }
    if (const auto* transport = std::get_if<EqualityElimination>(&proof.node)) {
        return "eq_elim(" + describe(*transport->equality) + ", " + describe(*transport->evidence) + ")";
    }
    return "refl";
}

} // namespace cppl::kernel
