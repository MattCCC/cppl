#pragma once

#include "cppl/kernel/proposition.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/vir/ids.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

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

// One obligation of a partial-correctness contract, with the contracts whose
// postconditions it supposes. Those must be established before it is used.
struct VerificationCondition {
    std::size_t obligation = 0;
    std::vector<std::size_t> callees; // indices into Program::contracts
};

struct ContractVerification {
    vir::FunctionId function;
    std::string name;
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

    // A body that is not one total core term: it contains a loop, or calls a
    // function whose contract is itself partial. Its contract states partial
    // correctness and holds when every condition below is proven and every
    // contract they suppose is established (SPEC.md 23, 24). Such a function is
    // never admitted as a core definition, so no theorem about its value
    // exists, and the fields above that describe one are unused.
    bool partial = false;
    std::vector<VerificationCondition> conditions;
    source::Digest identity; // content identity of the conditions, for callers
};

} // namespace cppl::obligations
