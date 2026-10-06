#include "body_walks.hpp"

#include "cppl/source/representation.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/types.hpp"

#include <algorithm>
#include <set>
#include <variant>
#include <vector>

namespace cppl::elaboration::detail {

// Every function a body calls, wherever the call stands: in a value, a
// condition, the rest of the body under a binding or a bound, a subscript's
// extent, or the arguments of a claim, which are never evaluated but must still
// be terms the formal core can state. Every node's children are visited, so a
// node kind added later is searched without being listed here.
void collect_callees(const vir::Expr& expr, std::vector<vir::SymbolId>& callees) {
    // A library summary is no function of this unit: what it states is a
    // trusted model assumption, recorded apart (RFC 0020 §6).
    // Nor is a validation's predicate probe: the program tests the value, and
    // what that establishes is RUNTIME-CHECKED, never a contract (SPEC.md
    // RUNTIMECHECK-011).
    if (const auto* call = std::get_if<vir::Call>(&expr.node);
        call != nullptr && !call->library.has_value() && !call->validation.has_value()) {
        callees.push_back(call->callee);
    }
    std::visit(
        [&callees](const auto& node) {
            if constexpr (requires { node.operands; }) {
                for (const vir::Expr& child : node.operands)
                    collect_callees(child, callees);
            }
            if constexpr (requires { node.arguments; }) {
                for (const vir::Expr& child : node.arguments)
                    collect_callees(child, callees);
            }
            if constexpr (requires { node.extent; }) {
                for (const vir::Expr& child : node.extent)
                    collect_callees(child, callees);
            }
            if constexpr (requires { node.body; }) {
                for (const vir::Expr& child : node.body)
                    collect_callees(child, callees);
            }
        },
        expr.node);
}

// The standard-library model a type is an instance of, recorded in `models`
// when it is one (RFC 0020 §10).
void note_model(const vir::Type& type, std::set<source::RepresentationKind>& models) {
    const source::RepresentationKind kind = type.representation.kind;
    if (source::is_sequence(kind) || kind == source::RepresentationKind::StdArray) {
        models.insert(kind);
    }
}

// Every standard-library model an expression's values are instances of. Every
// node's children are visited, so a node kind added later is searched without
// being listed here.
void collect_models(const vir::Expr& expr, std::set<source::RepresentationKind>& models) {
    note_model(expr.type, models);
    if (const auto* call = std::get_if<vir::Call>(&expr.node); call != nullptr && call->library.has_value()) {
        models.insert(call->library->container);
    }
    std::visit(
        [&models](const auto& node) {
            if constexpr (requires { node.operands; }) {
                for (const vir::Expr& child : node.operands)
                    collect_models(child, models);
            }
            if constexpr (requires { node.arguments; }) {
                for (const vir::Expr& child : node.arguments)
                    collect_models(child, models);
            }
            if constexpr (requires { node.extent; }) {
                for (const vir::Expr& child : node.extent)
                    collect_models(child, models);
            }
            if constexpr (requires { node.body; }) {
                for (const vir::Expr& child : node.body)
                    collect_models(child, models);
            }
        },
        expr.node);
}

// Whether a body passes through an unsafe block anywhere. Every statement-like
// node keeps the rest of its path among its operands, so searching them finds
// every region whatever node a later change adds.
bool contains_unsafe_region(const vir::Expr& expr) {
    if (std::holds_alternative<vir::UnsafeRegion>(expr.node)) {
        return true;
    }
    return std::visit(
        [](const auto& node) {
            if constexpr (requires { node.operands; }) {
                return std::ranges::any_of(node.operands,
                                           [](const vir::Expr& child) { return contains_unsafe_region(child); });
            } else {
                return false;
            }
        },
        expr.node);
}

} // namespace cppl::elaboration::detail
