// The routes a value takes to a call: the steps of each path through
// conditionals, and the value each step denotes.

#include "contracts_conditions.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/types.hpp"

#include <cstdint>
#include <ranges>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations::detail {

using contracts::Route;
using contracts::routes;
using contracts::Step;

namespace {

// The value a version was given on this path, or null if this path does not
// establish it. The steps taken so far are the path's version bindings, so this
// is the route-level counterpart of the resolution `lower_value` performs when
// it replays a read (SPEC.md 12.7).
const vir::Expr* established_value(const std::vector<Step>& steps, std::uint32_t version) {
    for (const auto& step : std::views::reverse(steps)) {
        if (step.binding != nullptr && step.binding->version == version)
            return step.value;
    }
    return nullptr;
}

// What a value denotes on this path, following reads of locals to the value
// that established them. A version's value reads only versions numbered below
// it, so following them strictly decreases the version and terminates; the
// bound is what the VIR already guarantees, not a fixed number of hops. A read
// this path does not establish - a loop's head version, a parameter - resolves
// to itself and is left opaque.
const vir::Expr& denoted_value(const vir::Expr& value, const std::vector<Step>& steps) {
    const vir::Expr* current = &value;
    for (const auto* read = std::get_if<vir::PlaceRef>(&current->node); read != nullptr;
         read = std::get_if<vir::PlaceRef>(&current->node)) {
        const vir::Expr* source = established_value(steps, read->version);
        if (source == nullptr)
            return *current;
        current = source;
    }
    return *current;
}

// Whether `version` is read anywhere in a conditional in `body`, in its
// condition or an arm. Such a read places this local's value beneath that
// conditional in the lowered value, which decides which of the two
// conditionals the route must split on first.
bool reads_from_a_conditional(const vir::Expr& body, std::uint32_t version) {
    bool found = false;
    const auto visit = [&](const vir::Expr& node, bool inside, const auto& self) -> void {
        if (found)
            return;
        if (const auto* read = std::get_if<vir::PlaceRef>(&node.node)) {
            found = found || (inside && read->version == version);
            return;
        }
        if (const auto* choice = std::get_if<vir::Conditional>(&node.node)) {
            if (choice->operands.size() == 3) {
                self(choice->operands[0], true, self);
                self(choice->operands[1], true, self);
                self(choice->operands[2], true, self);
                return;
            }
        }
        std::visit(
            [&](const auto& kind) {
                if constexpr (requires { kind.operands; }) {
                    for (const auto& operand : kind.operands)
                        self(operand, inside, self);
                } else if constexpr (requires { kind.arguments; }) {
                    for (const auto& argument : kind.arguments)
                        self(argument, inside, self);
                }
            },
            node.node);
    };
    visit(body, false, visit);
    return found;
}

// Bind a local whose value is, or denotes, a conditional. A conditional states
// one `select` term, of which neither arm's facts are known, so the route
// splits on the condition exactly as it does for a conditional in tail
// position: the local is bound to the arm this path takes, and owes its
// predicate under what that path supposes (SPEC.md 12.7).
//
// The split follows what the value denotes, so a conditional reached through
// any number of intervening locals splits just as a directly written one does.
// The local is bound to the arm itself, which is what it denotes on this route.
// An arm that is itself a conditional splits again, so only a non-conditional
// value is ever bound. Every expression a step points at is an existing
// subexpression of this tree, and resolution never crosses a version boundary:
// `established_value` takes the latest binding of that version on this path,
// which is the one current here.
//
// The order matters beyond this function. A route's conditions must correspond
// to the `select` nesting of the body's lowered value, because that nesting is
// what the proof is composed over: each `select` is discharged by the kernel's
// conditional elimination, and a leaf is proven under exactly the conditions
// that stand above it there. Splitting here in the order the conditionals are
// written keeps the two in step, since a read replays the value it was given.
bool bind_conditional(const vir::Expr& value, const vir::PlaceVersion& binding, const vir::Expr& body,
                      std::vector<Step> steps, std::vector<Route>& result) {
    const vir::Expr& denoted = denoted_value(value, steps);
    const auto* choice = std::get_if<vir::Conditional>(&denoted.node);
    if (choice == nullptr) {
        // The local denotes what this route resolved it to. That is the same
        // value a read of it replays, so binding it here states nothing new; it
        // only keeps the arm this route selected, which a `select` term over the
        // whole conditional would have discarded.
        steps.push_back(Step{&denoted, &binding, true});
        return routes(body, std::move(steps), result);
    }
    if (choice->operands.size() != 3 || !choice->operands[0].type.is_boolean())
        return false;
    for (const bool taken : {true, false}) {
        auto arm = steps;
        arm.push_back(Step{&choice->operands[0], nullptr, taken});
        if (!bind_conditional(choice->operands[taken ? 1 : 2], binding, body, std::move(arm), result))
            return false;
    }
    return true;
}

} // namespace

namespace contracts {

bool routes(const vir::Expr& expression, std::vector<Step> steps, std::vector<Route>& result) {
    if (result.size() >= 128)
        return false;
    if (const auto* bound = std::get_if<vir::PlaceVersion>(&expression.node)) {
        if (bound->operands.size() != 2)
            return false;
        // A conditional value states one `select` term, of which neither arm's
        // facts are known, so the route splits on its condition. What the value
        // denotes is what decides this, so a conditional reached through
        // intervening locals splits as a directly written one does.
        //
        // The split is taken here only when this binding is the outermost
        // `select` of what the body returns. A later binding that reads this one
        // from inside its own conditional puts this `select` beneath that one in
        // the lowered value, and the proof is composed over that nesting: the
        // conditions above a leaf there are what the leaf is proven under.
        // Splitting here as well would order the conditions differently from the
        // term and the composition would not line up, so it is left to the outer
        // conditional, which resolves this local's value through it (SPEC.md
        // 12.7). That case is refused today rather than proven.
        if (std::holds_alternative<vir::Conditional>(denoted_value(bound->operands[0], steps).node) &&
            !reads_from_a_conditional(bound->operands[1], bound->version)) {
            return bind_conditional(bound->operands[0], *bound, bound->operands[1], std::move(steps), result);
        }
        steps.push_back(Step{&bound->operands[0], bound, true});
        return routes(bound->operands[1], std::move(steps), result);
    }
    if (const auto* branch = std::get_if<vir::Conditional>(&expression.node)) {
        if (branch->operands.size() != 3)
            return false;
        steps.push_back(Step{&branch->operands[0], nullptr, true});
        if (!routes(branch->operands[1], steps, result))
            return false;
        steps.back().positive = false;
        return routes(branch->operands[2], std::move(steps), result);
    }
    result.push_back(Route{std::move(steps), &expression});
    return true;
}

} // namespace contracts

} // namespace cppl::obligations::detail
