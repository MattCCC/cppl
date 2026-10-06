#pragma once

#include "cppl/source/representation.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/types.hpp"

#include <set>
#include <vector>

// What an elaborated body uses, read off its VIR: the functions it calls, the
// standard-library models its values are instances of, and whether it passes
// through an unsafe block. Every node's children are visited, so a node kind
// added later is searched without being listed in any of these.
namespace cppl::elaboration::detail {

// Every function a body calls, wherever the call stands: in a value, a
// condition, the rest of the body under a binding or a bound, a subscript's
// extent, or the arguments of a claim, which are never evaluated but must still
// be terms the formal core can state.
void collect_callees(const vir::Expr& expr, std::vector<vir::SymbolId>& callees);

// The standard-library model a type is an instance of, recorded in `models`
// when it is one (RFC 0020 §10).
void note_model(const vir::Type& type, std::set<source::RepresentationKind>& models);

// Every standard-library model an expression's values are instances of.
void collect_models(const vir::Expr& expr, std::set<source::RepresentationKind>& models);

// Whether a body passes through an unsafe block anywhere.
[[nodiscard]] bool contains_unsafe_region(const vir::Expr& expr);

} // namespace cppl::elaboration::detail
