#include "cppl/kernel/proof.hpp"

#include <variant>

namespace cppl::kernel {

std::string describe(const ProofTerm& proof) {
    if (const auto* introduction = std::get_if<ForallIntroduction>(&proof.node)) {
        return "forall_intro(" + describe(introduction->binder) + ", " +
               describe(*introduction->body) + ")";
    }
    if (const auto* elimination = std::get_if<ForallElimination>(&proof.node)) {
        return "forall_elim(" + describe(*elimination->evidence) + ", " +
               describe(elimination->argument) + ")";
    }
    if (const auto* assumed = std::get_if<Hypothesis>(&proof.node)) {
        return "hypothesis(" + std::to_string(assumed->index.value) + ")";
    }
    if (const auto* introduction = std::get_if<ImplicationIntroduction>(&proof.node)) {
        return "implies_intro(" + describe(*introduction->premise) + ", " +
               describe(*introduction->body) + ")";
    }
    if (const auto* application = std::get_if<ImplicationElimination>(&proof.node)) {
        return "implies_elim(" + describe(*application->evidence) + ", " +
               describe(*application->premise) + ")";
    }
    return "refl";
}

}  // namespace cppl::kernel
