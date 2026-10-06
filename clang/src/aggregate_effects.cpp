#include "aggregate_values.hpp"
#include "cppl/clang/ast.hpp"
#include "places.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// What writes a whole struct value: a call handed it by reference, an
// assignment of another value, and the initialization of a local from one
// (TRUST.md TCB-AGGREGATE-001, TCB-AGGREGATE-002).
namespace cppl::clangbridge::detail::aggregates {

using bridge::anonymous_place;
using bridge::convert_type;
using bridge::designated_object;
using bridge::place_of;
using bridge::presumed_location;
using bridge::qualified_name_of;
using bridge::resolve_access;
using bridge::same_modeled_value;
using bridge::take;

namespace {

// A leaf of a struct argument's type that is not among the places this body
// tracks for it, if there is one: a write to the struct that this body follows
// leaf by leaf would leave that leaf reading the value it held before. A type
// whose leaves cannot be listed is answered with the struct itself.
std::optional<std::vector<PlaceStep>> untracked_leaf(const ArgumentGroup& group, const Locals& state) {
    std::vector<std::vector<PlaceStep>> expected;
    if (!leaf_paths(group.type, group.prefix, expected)) {
        return group.prefix;
    }
    for (std::vector<PlaceStep>& path : expected) {
        if (std::ranges::none_of(group.leaves, [&](std::size_t leaf) { return state[leaf].path == path; })) {
            return std::move(path);
        }
    }
    return std::nullopt;
}

// The path of `leaf` inside the struct `group` designates.
std::vector<PlaceStep> inside(const ArgumentGroup& group, const Local& leaf) {
    return {leaf.path.begin() + static_cast<std::ptrdiff_t>(group.prefix.size()), leaf.path.end()};
}

// Writes `values[at]` to `leaves[at]` and every leaf after it, one after
// another, each through the one write a member assignment makes, then lowers
// what follows.
std::optional<Expr> write_leaves(Lowering& body, const std::vector<std::size_t>& leaves, std::size_t at,
                                 const std::vector<Expr>& values, CXCursor statement, const Locals& locals,
                                 const Rest& rest) {
    if (at == leaves.size()) {
        return rest(locals);
    }
    return body.write_then(leaves[at], values[at], statement, locals, [&](const Locals& assigned) {
        return write_leaves(body, leaves, at + 1, values, statement, assigned, rest);
    });
}

} // namespace

// The implicit object is tracked one place per leaf, so it, or a member of it,
// is a group of places as a local is. A temporary, an object tracked as one
// place and storage this body does not track designate no group, and the
// reference positions then decide them as before.
std::optional<ArgumentGroup> argument_group(CXCursor argument, CXType bound, const Locals& state, const Frame& frame) {
    const CXCursor designated = designated_object(argument);
    const Type type = convert_type(clang_getCursorType(designated));
    if (!structural(type) || !same_modeled_value(type, convert_type(bound))) {
        return std::nullopt;
    }
    const std::optional<ResolvedAccess> access = resolve_access(designated);
    if (!access || access->dereferenced || !access->symbolic_indices.empty() ||
        (access->receiver &&
         (clang_Cursor_isNull(frame.receiver) != 0 || clang_equalCursors(access->declaration, frame.receiver) == 0))) {
        return std::nullopt;
    }
    ArgumentGroup group;
    group.declaration = access->declaration;
    group.prefix = access->path;
    group.type = type;
    for (std::size_t index = 0; index < state.size(); ++index) {
        const Local& entry = state[index];
        if (entry.is_deref() || entry.referent.has_value() || entry.binder.has_value() ||
            clang_equalCursors(entry.declaration, access->declaration) == 0 ||
            entry.path.size() < access->path.size() ||
            !std::equal(access->path.begin(), access->path.end(), entry.path.begin())) {
            continue;
        }
        // The object itself is one tracked place, not a group of them.
        if (entry.path.size() == access->path.size()) {
            return std::nullopt;
        }
        (entry.has_symbolic_step() ? group.others : group.leaves).push_back(index);
    }
    if (group.leaves.empty()) {
        return std::nullopt;
    }
    return group;
}

// A struct the callee may write -- through a mutable reference, or through any
// reference when its unsafe code may write what it is handed (TRUST.md
// TCB-UNSAFE-004) -- may have any of its places written, which reaches
// whatever may be one of them.
void reach_written(const std::vector<ArgumentGroup>& groups, bool unsafe_callee,
                   std::vector<std::size_t>& written_storage) {
    for (const ArgumentGroup& group : groups) {
        if (group.writable || unsafe_callee) {
            written_storage.insert(written_storage.end(), group.leaves.begin(), group.leaves.end());
            written_storage.insert(written_storage.end(), group.others.begin(), group.others.end());
        }
    }
}

// A struct handed by reference is one value the callee's contract states after
// the call. Where no write of the call can reach any of its places it is the
// value the call was made with, and its argument already is that. Otherwise the
// call leaves a fresh post-state value of the struct's type, of which only the
// callee's postcondition is supposed, and one object handed in several
// positions leaves one such value. A struct the callee may write has each leaf
// rebound to its member of that value: the leaf's declared type is charged
// there, as a write's is. A leaf another position of the call already gave its
// post-call version keeps that one, which describes the same storage. A struct
// the callee only reads keeps its leaves, each of which the alias model follows
// on its own (TRUST.md TCB-AGGREGATE-001, TCB-UNSAFE-004).
bool post_states(Lowering& body, const GroupCall& call, Locals& state, std::vector<std::size_t>& invalidated,
                 std::vector<CallEffect>& effects) {
    const auto handled = [&](std::size_t place) {
        return std::ranges::find(*call.targets, place) != call.targets->end() ||
               std::ranges::find(invalidated, place) != invalidated.end();
    };
    std::vector<std::pair<const ArgumentGroup*, std::uint32_t>> earlier_states;
    for (const ArgumentGroup& group : *call.groups) {
        const bool written = group.writable || call.unsafe_callee;
        if (!written && std::ranges::none_of(group.leaves, call.reached_by_a_write) &&
            std::ranges::none_of(group.others, call.reached_by_a_write)) {
            continue;
        }
        std::optional<std::uint32_t> version;
        for (const auto& [earlier, earlier_version] : earlier_states) {
            if (clang_equalCursors(earlier->declaration, group.declaration) != 0 && earlier->prefix == group.prefix) {
                version = earlier_version;
            }
        }
        if (!version.has_value()) {
            version = body.fresh_version();
            earlier_states.emplace_back(&group, *version);
        }
        effects.push_back(CallEffect{group.argument, *version, group.type});
        if (!written) {
            continue;
        }
        const std::string spelled = spelled_access(group.declaration, group.prefix);
        // Every leaf the call may write must be one this body follows: one it
        // does not track would keep reading the value it held before.
        if (const std::optional<std::vector<PlaceStep>> missing = untracked_leaf(group, state)) {
            body.reject("'" + spelled_access(group.declaration, *missing) + "' is not tracked here, so '" + spelled +
                        "', handed to '" + qualified_name_of(call.callee) +
                        "', which may write it, would keep reading the value it held before the call");
            return false;
        }
        for (const std::size_t leaf : group.leaves) {
            if (handled(leaf)) {
                continue;
            }
            Expr post;
            post.type = group.type;
            post.location = presumed_location(clang_getCursorLocation(call.cursor));
            post.node = PlaceRef{*version, anonymous_place("the post-state of '" + spelled + "'")};
            std::optional<Expr> member = member_at(std::move(post), inside(group, state[leaf]));
            if (!member.has_value()) {
                body.reject("'" + state[leaf].spelling + "' is not a member of '" + spelled +
                            "' this implementation can state after the call to '" + qualified_name_of(call.callee) +
                            "'");
                return false;
            }
            state[leaf].version = body.fresh_version();
            body.rebind(state[leaf].version, std::move(*member));
            body.establish(state[leaf].version);
            invalidated.push_back(leaf);
        }
        // An element place formed at a term names no member of the post-state
        // value, so it is unknown after the call.
        for (const std::size_t other : group.others) {
            if (handled(other)) {
                continue;
            }
            state[other].version = body.fresh_version();
            invalidated.push_back(other);
        }
    }
    return true;
}

// Each leaf of `a` is written its member of `b`, at its own declared type,
// exactly as assigning that member alone would write it, so a refined member
// owes its predicate and every place that may alias it goes stale (TRUST.md
// TCB-AGGREGATE-001, TCB-OBJ-003). C++ evaluates the right operand first (C++17
// [expr.ass]), so every member value is read before any member is written,
// which is also what a self-assignment reads. An assignment operator the
// program provides runs code whose effect is not modeled, and is refused by
// name.
std::optional<Expr> lower_whole_assignment(Lowering& body, CXCursor statement, const Locals& locals, const Frame& frame,
                                           const Rest& rest) {
    const CXCursor target = clang_Cursor_getArgument(statement, 0);
    const CXType assigned = clang_getCursorType(target);
    const std::string name = take(clang_getTypeSpelling(clang_getCanonicalType(assigned)));
    if (!structural(convert_type(assigned))) {
        return body.reject("assigning a whole value of type '" + name +
                           "' is not modeled: only a record or an array whose members are all modeled is assigned "
                           "whole in a verified body");
    }
    // The operator selected is one of the class's own copy or move assignment
    // operators, which this checks with every member's.
    if (std::optional<std::string> user = user_provided_copy(assigned, Copying::Assignment)) {
        return body.reject(*user);
    }
    Locals state = locals;
    std::vector<std::size_t> invalidated;
    // `std::move(b)` designates `b`, whose members a move C++ defines memberwise
    // reads as a copy would.
    std::optional<Expr> evaluated =
        body.evaluate(designated_object(clang_Cursor_getArgument(statement, 1)), state, invalidated);
    if (!evaluated) {
        return std::nullopt;
    }
    if (const auto* unsupported = std::get_if<Unsupported>(&evaluated->node)) {
        return body.reject(unsupported->reason);
    }
    if (std::holds_alternative<Conditional>(evaluated->node)) {
        return body.reject("assigning a conditional expression choosing between two values of type '" + name +
                           "' is not modeled");
    }
    const std::optional<ArgumentGroup> group = argument_group(target, assigned, state, frame);
    if (!group.has_value()) {
        // A place this body tracks as one, such as an object a reference
        // parameter designates, says for itself why it is not written.
        if (!body.written_local(target, state)) {
            return std::nullopt;
        }
        return body.reject("'" + name + "' is assigned where this body does not track it member by member");
    }
    const std::string spelled = spelled_access(group->declaration, group->prefix);
    if (!group->others.empty()) {
        return body.reject("'" + spelled +
                           "' is assigned while an element of it is selected at a term; assign it before forming one");
    }
    // Every leaf is written, so every leaf must be one this body follows, as for
    // a struct a call may write.
    if (const std::optional<std::vector<PlaceStep>> missing = untracked_leaf(*group, state)) {
        return body.reject("'" + spelled_access(group->declaration, *missing) + "' is not tracked where '" + spelled +
                           "' is assigned, so it would keep reading the value it held before");
    }
    std::optional<std::uint32_t> result;
    Expr whole = *evaluated;
    if (std::holds_alternative<Call>(evaluated->node)) {
        result = body.fresh_version();
        whole.node = PlaceRef{*result, anonymous_place("the value assigned to '" + spelled + "'")};
    }
    std::vector<Expr> values;
    for (const std::size_t leaf : group->leaves) {
        std::optional<Expr> member = member_at(whole, inside(*group, state[leaf]));
        if (!member.has_value()) {
            return body.reject("'" + state[leaf].spelling +
                               "' is not a member of the assigned value this implementation can state");
        }
        values.push_back(std::move(*member));
    }
    std::optional<Expr> lowered = write_leaves(body, group->leaves, 0, values, statement, state, rest);
    if (!lowered) {
        return std::nullopt;
    }
    for (const std::size_t changed : invalidated) {
        lowered = body.unknown(state, changed, std::move(*lowered), statement);
    }
    if (result.has_value()) {
        lowered = body.bind(*result, anonymous_place("the value assigned to '" + spelled + "'"), std::move(*evaluated),
                            std::move(*lowered), statement, {});
    }
    return lowered;
}

// Each leaf is bound to the value's member at its path, at the leaf's own
// declared type, so a refined member owes its predicate here exactly as one an
// aggregate initializer supplies does (SPEC.md 17.6, TRUST.md TCB-OBJ-003). A
// call is evaluated once and its result bound before any leaf reads it, so its
// effects happen once and every member is read from one value. A member of an
// object this body tracks member by member is that member's own version, and of
// any other value its projection (TRUST.md TCB-AGGREGATE-001).
std::optional<Expr> lower_initialization(Lowering& body, CXCursor declaration, const std::string& name,
                                         const Type& type, CXCursor initializer, const Locals& locals,
                                         const Rest& rest) {
    std::vector<TypeLeaf> leaves;
    if (auto refusal = body.type_leaves(type, name, leaves)) {
        return body.reject("local " + *refusal);
    }
    Locals declaring = locals;
    std::vector<std::size_t> invalidated;
    std::optional<Expr> evaluated = body.evaluate(initializer, declaring, invalidated);
    if (!evaluated) {
        return std::nullopt;
    }
    if (const auto* unsupported = std::get_if<Unsupported>(&evaluated->node)) {
        return body.reject(unsupported->reason);
    }
    if (!same_modeled_value(type, evaluated->type)) {
        return body.reject("initializing '" + name + "' of type '" + type.spelling + "' from '" +
                           evaluated->type.spelling + "' is a conversion that is not modeled");
    }
    if (std::holds_alternative<Conditional>(evaluated->node)) {
        return body.reject("initializing '" + name +
                           "' from a conditional expression choosing between two values of type '" + type.spelling +
                           "' is not modeled");
    }
    std::optional<std::uint32_t> result;
    Expr whole = *evaluated;
    if (std::holds_alternative<Call>(evaluated->node)) {
        result = body.fresh_version();
        whole.node = PlaceRef{*result, anonymous_place("the value initializing '" + name + "'")};
    }
    std::vector<std::uint32_t> versions;
    std::vector<Expr> values;
    const std::size_t first = declaring.size();
    for (const TypeLeaf& leaf : leaves) {
        std::optional<Expr> member = member_at(whole, leaf.path);
        if (!member.has_value()) {
            return body.reject("'" + leaf.spelling + "' is not a member of the value initializing '" + name +
                               "' this implementation can state");
        }
        versions.push_back(body.fresh_version());
        values.push_back(std::move(*member));
        declaring.push_back(Local{.declaration = declaration,
                                  .version = versions.back(),
                                  .type = leaf.type,
                                  .path = leaf.path,
                                  .spelling = leaf.spelling});
    }
    std::optional<Expr> lowered = rest(declaring);
    if (!lowered) {
        return std::nullopt;
    }
    for (std::size_t leaf = leaves.size(); leaf > 0; --leaf) {
        lowered = body.bind(versions[leaf - 1], place_of(declaring, first + leaf - 1), std::move(values[leaf - 1]),
                            std::move(*lowered), declaration, leaves[leaf - 1].type);
    }
    for (const std::size_t changed : invalidated) {
        lowered = body.unknown(declaring, changed, std::move(*lowered), declaration);
    }
    if (result.has_value()) {
        lowered = body.bind(*result, anonymous_place("the value initializing '" + name + "'"), std::move(*evaluated),
                            std::move(*lowered), declaration, {});
    }
    return lowered;
}

} // namespace cppl::clangbridge::detail::aggregates
