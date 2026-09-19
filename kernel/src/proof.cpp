#include "cppl/kernel/proof.hpp"

#include <variant>

namespace cppl::kernel {

std::string describe(const ProofTerm& proof) {
    if (const auto* introduction = std::get_if<ForallIntroduction>(&proof.node)) {
        return "forall_intro(" + describe(introduction->binder) + ", " +
               describe(*introduction->body) + ")";
    }
    return "refl";
}

}  // namespace cppl::kernel
