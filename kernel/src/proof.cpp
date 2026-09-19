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
    return "refl";
}

}  // namespace cppl::kernel
