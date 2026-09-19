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
    std::size_t conditions = 0;
};

struct PathCondition {
    kernel::Proposition actual;
    kernel::Proposition abstract;
    std::size_t calls = 0;
};

struct ReturnPath {
    std::vector<PathCondition> conditions;
    std::vector<CallVerification> calls;
    kernel::Term returned_value;
    std::size_t obligation = 0;
    kernel::Proposition reasoning_goal;
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
    std::vector<ReturnPath> paths;
    std::size_t obligation = 0;
};

}  // namespace cppl::obligations
