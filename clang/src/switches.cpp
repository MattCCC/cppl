#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "statements.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// `switch` in a verified body (C++ [stmt.switch]): its condition evaluated once
// and compared with each case value in order, each way into the body lowered
// from its label, and what follows the switch.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;

// A `switch` statement (C++ [stmt.switch]). Its condition is evaluated
// once, before any comparison, and that one value is compared with each
// case value in the order the labels appear: control enters the body at the
// first label whose value it equals, or else at `default:`, or else goes on
// with what follows the switch. From where it enters, the body runs to its
// end, through every later label, unless a `break`, `return` or `continue`
// leaves it first.
std::optional<Expr> BodyLowering::lower_switch(CXCursor statement, const Continuation& next, const Locals& locals,
                                               unsigned depth) {
    // libclang lists no init-statement among a switch's children, so one
    // left unseen would be a statement the program runs and the model
    // drops.
    const std::optional<SelectionHead> head = selection_head(statement);
    if (!head.has_value()) {
        return reject("the head of this 'switch' statement could not be read, so whether it holds an "
                      "init-statement is not known");
    }
    if (head->separator.has_value()) {
        return reject("a 'switch' statement with an init-statement is not modeled");
    }
    // The condition and the body, after the condition variable if one is
    // declared. Whatever the head holds starts where its parentheses
    // open: an init-statement no token shows, written through a macro,
    // would put the condition later.
    const std::vector<CXCursor> parts = children_of(statement);
    const bool declares = parts.size() == 3 && clang_getCursorKind(parts[0]) == CXCursor_VarDecl;
    const FilePosition opening = start_of(parts.front());
    if ((parts.size() != 2 && !declares) || clang_isExpression(clang_getCursorKind(parts[parts.size() - 2])) == 0 ||
        !stands_at(opening, head->file, head->first)) {
        return reject("the parts of this 'switch' statement could not be resolved");
    }
    const SwitchHeader header{statement, parts[parts.size() - 2], parts.back(), &next};
    if (!declares) {
        return lower_switch_dispatch(header, locals, depth);
    }
    // A condition variable is a local the condition's initializer
    // initializes, in scope through the whole body, and the condition is a
    // read of it (C++ [stmt.pre]).
    const std::vector<CXCursor> variable{parts[0]};
    Continuation dispatched;
    dispatched.dispatch = &header;
    return lower_declaration(variable, 0, dispatched, locals, depth);
}

// One `case` value, as a literal of the condition's type. It is a
// converted constant expression of that type (C++ [stmt.switch]), so Clang
// has converted it without narrowing and its value is one of the type's;
// what is read here is the value Clang evaluates, never one recomputed.
std::optional<Expr> BodyLowering::case_value(CXCursor value, const Type& type) {
    CXEvalResult evaluated = clang_Cursor_Evaluate(value);
    const bool integer = evaluated != nullptr && clang_EvalResult_getKind(evaluated) == CXEval_Int;
    const bool is_unsigned = integer && clang_EvalResult_isUnsignedInt(evaluated) != 0;
    const unsigned long long magnitude = is_unsigned ? clang_EvalResult_getAsUnsigned(evaluated) : 0;
    const long long signed_value = integer && !is_unsigned ? clang_EvalResult_getAsLongLong(evaluated) : 0;
    if (evaluated != nullptr) {
        clang_EvalResult_dispose(evaluated);
    }
    if (!integer) {
        return reject("a 'case' value Clang does not evaluate to an integer is not modeled");
    }
    Expr literal;
    literal.type = type;
    literal.location = presumed_location(clang_getCursorLocation(value));
    literal.node = IntLiteral{is_unsigned ? static_cast<std::int64_t>(magnitude) : signed_value};
    return literal;
}

std::optional<Expr> BodyLowering::lower_switch_dispatch(const SwitchHeader& header, const Locals& locals,
                                                        unsigned depth) {
    // The body's statements with every label taken off them, in order,
    // and the labels, each with the position it leads into. A label's
    // statement is the one it is written on; the statements after it in
    // the body follow it.
    struct Entry {
        CXCursor label;
        std::optional<CXCursor> value; // none for `default:`
        std::size_t position = 0;
    };
    std::vector<CXCursor> written;
    if (clang_getCursorKind(header.body) == CXCursor_CompoundStmt) {
        written = children_of(header.body);
    } else {
        written.push_back(header.body);
    }
    std::vector<Entry> entries;
    std::vector<CXCursor> statements;
    std::vector<std::size_t> positions;
    for (CXCursor statement : written) {
        if (entries.empty() && !is_switch_label(statement)) {
            return reject("a statement before the first label of a 'switch' is never executed, and is not "
                          "modeled");
        }
        while (is_switch_label(statement)) {
            const std::vector<CXCursor> label = children_of(statement);
            const bool fallback = clang_getCursorKind(statement) == CXCursor_DefaultStmt;
            if (!fallback && label.size() == 3) {
                return reject("a case range, 'case low ... high:', is not modeled");
            }
            if (label.size() != (fallback ? 1U : 2U)) {
                return reject("a label of this 'switch' could not be resolved");
            }
            entries.push_back(
                Entry{statement, fallback ? std::nullopt : std::optional<CXCursor>{label.front()}, statements.size()});
            positions.push_back(statements.size());
            statement = label.back();
        }
        // A label anywhere else is a way into the middle of a statement
        // that lowering it from its start never takes (Duff's device).
        if (holds_switch_label(statement)) {
            return reject("a 'case' or 'default' label inside a nested statement of its 'switch' is not modeled");
        }
        statements.push_back(statement);
    }

    // The condition, evaluated once, with any call in it and that call's
    // effects, and bound to one version every comparison reads.
    Locals state = locals;
    std::vector<std::size_t> invalidated;
    std::optional<Expr> value = evaluate(header.condition, state, invalidated);
    if (!value) {
        return std::nullopt;
    }
    const Type condition = value->type;
    const std::uint32_t version = next_version++;
    std::vector<std::optional<Expr>> literals;
    for (const Entry& entry : entries) {
        if (!entry.value.has_value()) {
            literals.emplace_back();
            continue;
        }
        std::optional<Expr> literal = case_value(*entry.value, condition);
        if (!literal) {
            return std::nullopt;
        }
        literals.push_back(std::move(literal));
    }

    // Each way into the body, lowered from its label, inside the switch.
    SwitchFrame frame{header.exit, frames.size(), switch_frames.size()};
    Continuation leave;
    leave.left = &frame;
    switch_frames.push_back(&frame);
    std::vector<Expr> entered;
    for (const Entry& entry : entries) {
        Continuation from{&leave, &statements, entry.position};
        from.labels = &positions;
        std::optional<Expr> lowered = lower_statements(from, state, depth + 1);
        if (!lowered) {
            switch_frames.pop_back();
            return std::nullopt;
        }
        entered.push_back(std::move(*lowered));
    }
    switch_frames.pop_back();

    // No value matches: `default:`, or else what follows the switch.
    const auto fallback = std::ranges::find_if(entries, [](const Entry& entry) { return !entry.value.has_value(); });
    std::optional<Expr> chain;
    if (fallback != entries.end()) {
        chain = std::move(entered[static_cast<std::size_t>(fallback - entries.begin())]);
    } else {
        chain = lower_statements(*header.exit, state, depth + 1);
    }
    if (!chain) {
        return std::nullopt;
    }
    Type truth;
    truth.kind = TypeKind::Bool;
    truth.spelling = "bool";
    for (std::size_t index = entries.size(); index-- > 0;) {
        std::optional<Expr>& literal = literals[index];
        if (!literal.has_value()) {
            continue;
        }
        if (return_paths(entered[index]) + return_paths(*chain) > kMaxReturnPaths) {
            return reject("more than " + std::to_string(kMaxReturnPaths) + " return paths are not modeled");
        }
        const source::SourceLocation at = presumed_location(clang_getCursorLocation(entries[index].label));
        Expr read;
        read.type = condition;
        read.location = at;
        read.node = PlaceRef{version, anonymous_place("switch condition")};
        Expr matches;
        matches.type = truth;
        matches.location = at;
        matches.node = Binary{BinaryOp::Equal, {std::move(read), std::move(*literal)}};
        Expr branch;
        branch.type = chain->type;
        branch.location = at;
        branch.node = Conditional{{std::move(matches), std::move(entered[index]), std::move(*chain)}};
        chain = std::move(branch);
    }
    Expr body = std::move(*chain);
    for (const std::size_t changed : invalidated) {
        body = unknown(state, changed, std::move(body), header.statement);
    }
    return bind(version, anonymous_place("switch condition"), std::move(*value), std::move(body), header.statement);
}

// What follows a switch, reached by a `break` belonging to it or by the
// end of its body, lowered outside the switch: a `break` there belongs to
// whatever encloses the switch.
std::optional<Expr> BodyLowering::leave_switch(const SwitchFrame& frame, const Locals& locals, unsigned depth) {
    const std::vector<const SwitchFrame*> inside = switch_frames;
    switch_frames.resize(std::min(switch_frames.size(), frame.switches_outside));
    std::optional<Expr> after = lower_statements(*frame.exit, locals, depth + 1);
    switch_frames = inside;
    return after;
}

} // namespace cppl::clangbridge::detail
