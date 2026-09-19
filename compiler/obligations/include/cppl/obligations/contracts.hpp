#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "cppl/kernel/proposition.hpp"
#include "cppl/vir/ids.hpp"

namespace cppl::obligations {

struct CallVerification {
    vir::FunctionId callee;
    std::string callee_name;
    std::vector<kernel::Term> arguments;
    kernel::Term value;
    kernel::Type result;
    // Scope: caller parameters, earlier call results, this call's result.
    kernel::Proposition postcondition;
    std::optional<std::size_t> precondition_obligation;
    std::optional<kernel::Proposition> reasoning_goal;
};

struct ContractVerification {
    vir::FunctionId function;
    std::vector<kernel::Type> parameters;
    std::optional<kernel::Proposition> precondition;
    // Scope: function parameters followed by its specification-only result.
    kernel::Proposition postcondition;
    kernel::Type result;
    kernel::Term returned_value;
    kernel::Term named_value;
    kernel::Proposition theorem;
    std::vector<CallVerification> calls;
    std::size_t obligation = 0;
    kernel::Proposition reasoning_goal;
};

}  // namespace cppl::obligations
