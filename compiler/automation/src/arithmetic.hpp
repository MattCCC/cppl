#pragma once

#include "cppl/kernel/check.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"

#include <optional>

namespace cppl::automation {

// Evidence for `goal` that introduces its quantifiers and premises and closes
// the equality underneath by linear arithmetic over the premises. With
// `rewriting`, equalities among the premises, and equalities between variables
// that arithmetic establishes, are first used to rewrite the goal.
[[nodiscard]] std::optional<kernel::ProofTerm> arithmetic_evidence(const kernel::Context& context,
                                                                   const kernel::Proposition& goal, bool rewriting);

} // namespace cppl::automation
