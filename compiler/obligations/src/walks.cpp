#include "walks.hpp"

#include "cppl/vir/expr.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace cppl::obligations::detail {

// The calls an expression evaluates, in evaluation order. A read of a local is
// not one of them: the call its value came from was evaluated where the local
// was written, and is collected there.
void collect_calls(const vir::Expr& expression, const Contracts& contracts, std::vector<const vir::Expr*>& calls) {
    if (const auto* call = std::get_if<vir::Call>(&expression.node)) {
        for (const auto& argument : call->arguments) {
            collect_calls(argument, contracts, calls);
        }
        // A library call is evaluated like a verified one, against its
        // trusted summary rather than a contract (RFC 0020 §6), and so is a
        // validation, against the test it performs (SPEC.md RUNTIMECHECK-011).
        if (contracts.contains(call->callee.usr) || call->library.has_value() || call->validation.has_value()) {
            calls.push_back(&expression);
        }
    } else if (const auto* returned = std::get_if<vir::ReturnState>(&expression.node)) {
        for (const auto& operand : returned->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* unknown = std::get_if<vir::UnknownVersion>(&expression.node)) {
        for (const auto& operand : unknown->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* bound = std::get_if<vir::PlaceVersion>(&expression.node)) {
        for (const auto& operand : bound->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* bounded = std::get_if<vir::ElementBound>(&expression.node)) {
        // The extent is a term of its own, so a call inside it is a call this
        // body makes and must be collected with the rest.
        for (const auto& extent : bounded->extent)
            collect_calls(extent, contracts, calls);
        for (const auto& operand : bounded->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* binary = std::get_if<vir::Binary>(&expression.node)) {
        for (const auto& operand : binary->operands) {
            collect_calls(operand, contracts, calls);
        }
    } else if (const auto* negation = std::get_if<vir::Negation>(&expression.node)) {
        for (const auto& operand : negation->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* minus = std::get_if<vir::Minus>(&expression.node)) {
        for (const auto& operand : minus->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* conversion = std::get_if<vir::Conversion>(&expression.node)) {
        for (const auto& operand : conversion->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* aggregate = std::get_if<vir::Aggregate>(&expression.node)) {
        for (const auto& operand : aggregate->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* branch = std::get_if<vir::Conditional>(&expression.node)) {
        for (const auto& operand : branch->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* loop = std::get_if<vir::Loop>(&expression.node)) {
        for (const auto& operand : loop->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* next = std::get_if<vir::Iterate>(&expression.node)) {
        for (const auto& operand : next->operands)
            collect_calls(operand, contracts, calls);
    } else if (const auto* region = std::get_if<vir::UnsafeRegion>(&expression.node)) {
        // The block's own calls are not lowered; these are the path's after it.
        for (const auto& operand : region->operands)
            collect_calls(operand, contracts, calls);
    }
}

// Whether the condition selecting a path makes a verified call. The total walk
// lowers such a call to its callee's definition and supposes nothing its
// contract proves, so no fact of that contract would reach an obligation the
// path owes; the conditions walk supposes the postcondition where the call is
// made (SPEC.md RUNTIMECHECK-006, VERIFIED-014).
bool calls_in_condition(const vir::Expr& expression, const Contracts& contracts) {
    bool found = false;
    const auto visit = [&](const auto& operands) {
        for (const auto& operand : operands) {
            found = found || calls_in_condition(operand, contracts);
        }
    };
    std::visit(
        [&](const auto& node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, vir::Call>) {
                visit(node.arguments);
            } else if constexpr (std::is_same_v<Node, vir::ElementBound>) {
                visit(node.extent);
                visit(node.operands);
            } else if constexpr (std::is_same_v<Node, vir::Conditional>) {
                std::vector<const vir::Expr*> calls;
                collect_calls(node.operands.front(), contracts, calls);
                found = std::ranges::any_of(calls, [](const vir::Expr* site) {
                    const auto& call = std::get<vir::Call>(site->node);
                    return !call.library.has_value() && !call.validation.has_value();
                });
                visit(node.operands);
            } else if constexpr (requires { node.operands; }) {
                visit(node.operands);
            }
        },
        expression.node);
    return found;
}

bool requires_conditions(const vir::Expr& expression) {
    if (std::holds_alternative<vir::Loop>(expression.node) ||
        std::holds_alternative<vir::ReturnState>(expression.node) ||
        std::holds_alternative<vir::UnknownVersion>(expression.node) ||
        // A bound states an obligation, which only the path walk emits.
        std::holds_alternative<vir::ElementBound>(expression.node) ||
        // So does a path claimed not to occur, which also has no value.
        std::holds_alternative<vir::PathContradiction>(expression.node) ||
        // A case split is paths, one per state, and no value.
        std::holds_alternative<vir::CaseSplit>(expression.node) ||
        // An unsafe block is not modeled, so no term states what it does.
        std::holds_alternative<vir::UnsafeRegion>(expression.node)) {
        return true;
    }
    if (const auto* bound = std::get_if<vir::PlaceVersion>(&expression.node)) {
        return std::ranges::any_of(bound->operands, requires_conditions);
    }
    if (const auto* branch = std::get_if<vir::Conditional>(&expression.node)) {
        return std::ranges::any_of(branch->operands, requires_conditions);
    }
    return false;
}

void collect_callees(const vir::Expr& expr, std::set<std::string>& callees) {
    if (const auto* call = std::get_if<vir::Call>(&expr.node)) {
        callees.insert(call->callee.usr);
        for (const vir::Expr& argument : call->arguments) {
            collect_callees(argument, callees);
        }
        return;
    }
    if (const auto* binary = std::get_if<vir::Binary>(&expr.node)) {
        for (const vir::Expr& operand : binary->operands) {
            collect_callees(operand, callees);
        }
    }
    if (const auto* branch = std::get_if<vir::Conditional>(&expr.node)) {
        for (const auto& operand : branch->operands)
            collect_callees(operand, callees);
    }
    if (const auto* bound = std::get_if<vir::PlaceVersion>(&expr.node)) {
        for (const auto& operand : bound->operands)
            collect_callees(operand, callees);
    }
    if (const auto* negation = std::get_if<vir::Negation>(&expr.node)) {
        for (const auto& operand : negation->operands)
            collect_callees(operand, callees);
    }
    if (const auto* minus = std::get_if<vir::Minus>(&expr.node)) {
        for (const auto& operand : minus->operands)
            collect_callees(operand, callees);
    }
    if (const auto* conversion = std::get_if<vir::Conversion>(&expr.node)) {
        for (const auto& operand : conversion->operands)
            collect_callees(operand, callees);
    }
    if (const auto* aggregate = std::get_if<vir::Aggregate>(&expr.node)) {
        for (const auto& operand : aggregate->operands)
            collect_callees(operand, callees);
    }
    if (const auto* loop = std::get_if<vir::Loop>(&expr.node)) {
        for (const auto& operand : loop->operands)
            collect_callees(operand, callees);
    }
    if (const auto* next = std::get_if<vir::Iterate>(&expr.node)) {
        for (const auto& operand : next->operands)
            collect_callees(operand, callees);
    }
}

} // namespace cppl::obligations::detail
