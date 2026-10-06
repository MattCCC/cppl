#pragma once

#include "cppl/vir/expr.hpp"
#include "cppl/vir/module.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

// What a body evaluates, read off its VIR: the calls it makes, the functions
// it reaches, and whether its paths must be walked one by one rather than
// stated as one term.
namespace cppl::obligations::detail {

// The verified functions of a module by symbol, whose contracts a call is
// checked against.
using Contracts = std::map<std::string, const vir::Function*>;

// The calls an expression evaluates, in evaluation order.
void collect_calls(const vir::Expr& expression, const Contracts& contracts, std::vector<const vir::Expr*>& calls);

// Whether the condition selecting a path makes a verified call.
[[nodiscard]] bool calls_in_condition(const vir::Expr& expression, const Contracts& contracts);

// Whether an expression has paths the total walk cannot state as one term.
[[nodiscard]] bool requires_conditions(const vir::Expr& expression);

// Every function a pure body calls, by symbol.
void collect_callees(const vir::Expr& expr, std::set<std::string>& callees);

} // namespace cppl::obligations::detail
