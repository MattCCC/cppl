#pragma once

#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/vir/expr.hpp"
#include "lowering.hpp"

#include <expected>
#include <functional>
#include <vector>

// A struct value a body assembles from its members (`vir::Aggregate`), as the
// obligations state it (TRUST.md TCB-AGGREGATE-001). The formal core has no
// constructor for a struct value, so an assembled value is a fresh value of the
// struct's type, bound on the path that evaluates it, of which only the facts
// stated here are supposed.
namespace cppl::obligations::detail {

// Every struct value an expression assembles from its members, outermost first
// and in the order they are written. One nested in another is a member of it
// and is not collected on its own: it is stated as part of the one that holds
// it.
void collect_aggregates(const vir::Expr& expression, std::vector<const vir::Expr*>& sites);

// Whether a body assembles a struct value anywhere. Such a value has no term of
// its own, so the body has no total term either: it is verified path by path,
// where each evaluation binds the value it assembles.
[[nodiscard]] bool contains_aggregate(const vir::Expr& expression);

// A member value lowered in the scope the assembled value is bound in, before
// its binder.
using MemberLowering = std::function<std::expected<kernel::Term, Failure>(const vir::Expr&)>;

// Where a path binds an assembled value: the expression assembling it, its type
// and what is supposed of it.
using BindAssembled =
    std::function<void(const vir::Expr& site, const kernel::Type& type, std::vector<kernel::Proposition> facts)>;

// Binds each struct value `expression` assembles that `bound` does not bind yet,
// in order, each through `bind` before the next is stated: a fresh value of its
// type, of which the path supposes, for each scalar leaf in member order, only
// that its projection is the member value it was assembled from, lowered by
// `lower` and moved past the binder. The member values are reads of places,
// never calls, so nothing the expression evaluates is passed over by binding
// them before any call it makes.
[[nodiscard]] std::expected<void, Failure> bind_assembled(const vir::Expr& expression, const CallBindings& bound,
                                                          const MemberLowering& lower, const BindAssembled& bind);

} // namespace cppl::obligations::detail
