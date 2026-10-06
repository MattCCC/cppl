#include "cppl/clang/ast.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "statements.hpp"

#include <clang-c/Index.h>
#include <cstddef>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

// What a value C++ selects by a condition is lowered to where it stands as a
// whole statement's value (C++ [expr.cond], [expr.log.and], [expr.log.or]): a
// returned `?:`, `&&` or `||` is a return on each route its condition selects,
// and a declaration of one local or an assignment computing one whose arm
// reads through a subscript or a pointer is lowered once on each such route,
// that route's arm evaluated in place of the selection (statements.hpp).
namespace cppl::clangbridge::detail {

// A returned value. `c ? a : b`, `a && b` and `a || b` return what the arm
// their condition selects evaluates to (C++ [expr.cond], [expr.log.and],
// [expr.log.or]), so each arm is a return of its own on the routes that reach
// it, and what an arm reads through a subscript or a pointer is formed, and
// owes its bound or capability, only where it is evaluated.
std::optional<Expr> BodyLowering::lower_returned(CXCursor value, CXCursor statement, const Locals& locals,
                                                 unsigned depth) {
    if (const auto selected = signature.clause ? std::nullopt : selected_value(value)) {
        const auto arm = [&](std::optional<CXCursor> part) -> Branch {
            return [&, part](const Locals& state) -> std::optional<Expr> {
                return part ? lower_returned(*part, statement, state, depth + 1)
                            : completed(selected->constant, state, statement);
            };
        };
        return lower_condition(selected->condition, arm(selected->when_true), arm(selected->when_false), locals,
                               depth + 1);
    }
    return forming(statement, [&]() -> std::optional<Expr> {
        Locals state = locals;
        std::vector<std::size_t> invalidated;
        auto returned = evaluate(value, state, invalidated);
        if (!returned)
            return std::nullopt;
        const auto* call = std::get_if<Call>(&returned->node);
        if (call == nullptr || call->effects.empty())
            return completed(std::move(*returned), state, statement);
        const auto version = next_version++;
        Expr read;
        read.type = returned->type;
        read.location = returned->location;
        read.node = PlaceRef{version, anonymous_place("return value")};
        Expr body = completed(std::move(read), state, statement);
        for (auto changed : invalidated)
            body = unknown(state, changed, std::move(body), statement);
        return bind(version, anonymous_place("return value"), std::move(*returned), std::move(body), statement);
    });
}

// The selected value `statement` computes that it is lowered once per route
// for, unless this route already evaluates one of its arms in its place.
std::optional<SelectedValue> BodyLowering::selection_to_split(CXCursor statement) const {
    auto selected = signature.clause ? std::nullopt : selected_reading(statement, chosen_arms);
    if (!selected || chosen_for(chosen_arms, selected->selection) != nullptr) {
        return std::nullopt;
    }
    return selected;
}

std::optional<Expr> BodyLowering::lower_selected_statement(CXCursor statement, const SelectedValue& selected,
                                                           const Continuation& next, const Locals& locals,
                                                           unsigned depth) {
    const auto route = [&](std::optional<CXCursor> arm) -> Branch {
        return [&, arm](const Locals& state) {
            chosen_arms.push_back(ChosenArm{selected.selection, arm, selected.constant});
            std::optional<Expr> lowered =
                forming(statement, [&] { return lower_statement_form(statement, next, state, depth + 1); });
            chosen_arms.pop_back();
            return lowered;
        };
    };
    return lower_condition(selected.condition, route(selected.when_true), route(selected.when_false), locals,
                           depth + 1);
}

} // namespace cppl::clangbridge::detail
