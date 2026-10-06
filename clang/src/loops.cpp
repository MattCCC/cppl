#include "access.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/source/location.hpp"
#include "expressions.hpp"
#include "formal.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "types.hpp"
#include "unsafe.hpp"
#include "write_scan.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// Loops in a verified body (SPEC.md 24, LOOP-001 to LOOP-006, TERMINATION-004):
// the clauses the projector declared at a loop's head, its entry, its head with
// every local it writes carried, one iteration, the end of each iteration, and
// `break`.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::take;

// Whether this statement is the declaration the projector emitted to force
// a templated function's contract probes to be instantiated alongside it.
//
// Every such declaration is generated, so it is recognized by the
// projector's own prefix, which no ordinary declaration may use.
bool BodyLowering::is_instantiation_marker(CXCursor statement) const {
    if (invariant_prefix.empty() || clang_getCursorKind(statement) != CXCursor_DeclStmt) {
        return false;
    }
    const std::vector<CXCursor> declared = children_of(statement);
    return std::ranges::all_of(
               declared,
               [&](CXCursor candidate) {
                   return clang_getCursorKind(candidate) == CXCursor_VarDecl &&
                          take(clang_getCursorSpelling(candidate)).starts_with(invariant_prefix + "force_");
               }) &&
           !declared.empty();
}

// The generated declaration a loop invariant was projected into, if the
// statement is one.
std::optional<BodyLowering::LoopMarker> BodyLowering::invariant_marker(CXCursor statement) const {
    if (invariant_prefix.empty() || clang_getCursorKind(statement) != CXCursor_DeclStmt) {
        return std::nullopt;
    }
    const std::vector<CXCursor> declared = children_of(statement);
    if (declared.size() != 1 || clang_getCursorKind(declared[0]) != CXCursor_VarDecl) {
        return std::nullopt;
    }
    const std::string name = take(clang_getCursorSpelling(declared[0]));
    if (name.starts_with(invariant_prefix + "invariant_")) {
        return LoopMarker{declared[0], false};
    }
    if (name.starts_with(invariant_prefix + "measure_")) {
        return LoopMarker{declared[0], true};
    }
    return std::nullopt;
}

// A loop, as its entry, its head, one iteration, and what follows it
// (SPEC.md 24). Every local the loop writes is carried: from the head on it
// denotes a fresh version, of which only the invariants and the condition
// are known. A local the loop does not write keeps the version it had.
std::optional<Expr> BodyLowering::lower_loop(const LoopHeader& header, const Locals& locals, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        return reject("more than " + std::to_string(kMaxExpressionDepth) +
                      " nested or consecutive statements on one path are not modeled");
    }
    std::vector<CXCursor> statements;
    if (clang_getCursorKind(header.body) == CXCursor_CompoundStmt) {
        statements = children_of(header.body);
    } else {
        statements.push_back(header.body);
    }
    std::vector<CXCursor> markers;
    // A lexicographic measure is one marker per component, in the order
    // written (SPEC.md TERMINATION-004).
    std::vector<CXCursor> measure_markers;
    std::size_t first = 0;
    while (first < statements.size()) {
        const std::optional<LoopMarker> marker = invariant_marker(statements[first]);
        if (!marker) {
            break;
        }
        (marker->measure ? measure_markers : markers).push_back(marker->cursor);
        ++first;
    }

    LoopFrame frame;
    frame.id = next_loop++;
    frame.statement = header.statement;
    frame.head = locals;
    frame.increment = header.increment;
    frame.exit = header.exit;
    frame.frames_outside = frames.size();
    frame.condition = header.condition;
    frame.condition_last = header.condition_last;
    frame.range = header.range;
    std::vector<bool> written(locals.size(), false);
    if (header.range != nullptr) {
        mark_range_writes(*header.range, locals, written);
    }
    if (clang_Cursor_isNull(header.condition) == 0) {
        mark_writes(header.condition, locals, written);
    }
    if (header.increment) {
        mark_writes(*header.increment, locals, written);
    }
    mark_writes(header.body, locals, written);
    if (clang_Cursor_isNull(header.condition) == 0) {
        mark_sequence_writes(header.condition, locals, written);
    }
    if (header.increment) {
        mark_sequence_writes(*header.increment, locals, written);
    }
    mark_sequence_writes(header.body, locals, written);
    // An unsafe block in the loop may write whatever it reaches on any
    // iteration, so each such place is carried: at the head it is a fresh
    // value no fact from before the loop describes (SPEC.md LOOP-005). And
    // an iteration, like what follows the loop, may come after the block,
    // so none of them holds a contract's capability.
    const std::vector<CXCursor> unsafe_inside = unsafe_blocks_in(header.body, invariant_prefix);
    if (!unsafe_inside.empty()) {
        for (const std::size_t index : unsafe_reach(locals)) {
            written[index] = true;
        }
    }
    const std::optional<source::SourceLocation> enclosing_revocation = revoked_by;
    if (!unsafe_inside.empty() && !revoked_by.has_value()) {
        if (const std::optional<CXCursor> marker = unsafe_marker_of(unsafe_inside.front(), invariant_prefix)) {
            revoked_by = presumed_location(clang_getCursorLocation(*marker));
        }
    }
    for (std::size_t index = 0; index < locals.size(); ++index) {
        if (written[index]) {
            frame.carried.push_back(index);
            // A container an iteration may reallocate has, at the head, a
            // generation no view formed before the loop was formed at.
            new_generation(frame.head[index],
                           "the loop at " + describe_location(header.statement) + ", which may change it");
        }
    }

    std::vector<Expr> invariants;
    for (const CXCursor marker : markers) {
        const CXCursor initializer = clang_Cursor_getVarDeclInitializer(marker);
        if (clang_Cursor_isNull(initializer) != 0) {
            return reject("a loop invariant was not resolved");
        }
        // A range-based for's invariant holds at the head, before the loop
        // variable is initialized for the iteration (SPEC.md LOOP-004).
        if (header.range != nullptr &&
            named_declarations(initializer).contains(clang_hashCursor(header.range->variable))) {
            return reject("an invariant of the range-based for at " + describe_location(header.statement) +
                          " names its loop variable '" + take(clang_getCursorSpelling(header.range->variable)) +
                          "', which it holds before: the invariant holds at each iteration's head, before the "
                          "loop variable is initialized (SPEC.md LOOP-004)");
        }
        // One stating an implication or an equivalence is the proposition its
        // form builds; any other is the condition it is.
        const std::string named = take(clang_getCursorSpelling(marker));
        const Selection::InvariantForm* form = nullptr;
        if (invariant_forms != nullptr) {
            const auto found = std::ranges::find(*invariant_forms, named, &Selection::InvariantForm::name);
            form = found == invariant_forms->end() ? nullptr : &*found;
        }
        Expr invariant;
        if (form != nullptr) {
            std::expected<Expr, std::string> stated = build_invariant(initializer, form->shape, signature, frame.head);
            if (!stated) {
                return reject("a loop invariant: " + stated.error());
            }
            invariant = std::move(*stated);
        } else {
            invariant = build_expression(initializer, signature, frame.head, 0);
            if (!std::holds_alternative<Unsupported>(invariant.node) && invariant.type.kind != TypeKind::Bool) {
                return reject("a loop invariant must be a condition");
            }
        }
        invariants.push_back(std::move(invariant));
        consumed_invariants.push_back(take(clang_getCursorSpelling(marker)));
    }
    // Each measure component is read in the head's scope like an invariant,
    // but it is a value rather than a condition. Its well-founded domain is
    // checked where the obligation is stated (SPEC.md 22.5).
    std::vector<Expr> measures;
    for (const CXCursor marker : measure_markers) {
        const CXCursor initializer = clang_Cursor_getVarDeclInitializer(marker);
        if (clang_Cursor_isNull(initializer) != 0) {
            return reject("a loop measure was not resolved");
        }
        Expr value = build_expression(initializer, signature, frame.head, 0);
        if (!std::holds_alternative<Unsupported>(value.node) && value.type.kind != TypeKind::Int) {
            return reject("a loop measure must be an integer");
        }
        measures.push_back(std::move(value));
        consumed_invariants.push_back(take(clang_getCursorSpelling(marker)));
    }
    // A range-based for written without a measure states the one C++ gives
    // it: the positions left (SPEC.md TERMINATION-004).
    if (header.range != nullptr && measures.empty()) {
        measures.push_back(range_measure(*header.range, frame.head));
    }

    const std::vector<CXCursor> rest(statements.begin() + static_cast<std::ptrdiff_t>(first), statements.end());
    Continuation iteration;
    iteration.iteration = &frame;
    // One iteration from the head, and what follows the loop.
    const Branch iterate = [&](const Locals& state) {
        frames.push_back(&frame);
        std::optional<Expr> once =
            header.range != nullptr
                ? lower_range_iteration(*header.range, Continuation{&iteration, &rest, 0}, state, depth + 1)
                : lower_statements(Continuation{&iteration, &rest, 0}, state, depth + 1);
        frames.pop_back();
        return once;
    };
    const Branch leave = [&](const Locals& state) {
        return lower_statements(*header.exit, state, depth + 1);
    };

    // What happens from the head on. A `do` loop runs its body first and
    // decides at each iteration's end; a `for` without a condition always
    // runs it, and is left only by a `break` or a `return` (SPEC.md
    // LOOP-001). Otherwise the condition decides before each iteration,
    // split into the routes it selects as an `if`'s is.
    std::optional<Expr> decided;
    if (header.condition_last || (clang_Cursor_isNull(header.condition) != 0 && header.range == nullptr)) {
        decided = iterate(frame.head);
    } else if (header.range == nullptr) {
        decided = lower_condition(header.condition, iterate, leave, frame.head, depth + 1);
    } else if (std::optional<Expr> once = iterate(frame.head); once) {
        if (std::optional<Expr> after = leave(frame.head); after) {
            decided.emplace();
            decided->type = result_type;
            decided->node =
                Conditional{{range_condition(*header.range, frame.head), std::move(*once), std::move(*after)}};
        }
    }
    revoked_by = enclosing_revocation;
    if (!decided) {
        return std::nullopt;
    }
    if (return_paths(*decided) > kMaxReturnPaths) {
        return reject("more than " + std::to_string(kMaxReturnPaths) + " return paths are not modeled");
    }
    const source::SourceLocation location = presumed_location(clang_getCursorLocation(header.statement));
    Expr head = std::move(*decided);
    head.location = location;

    Loop loop;
    loop.loop = frame.id;
    for (const std::size_t index : frame.carried) {
        loop.heads.push_back(frame.head[index].version);
        loop.places.push_back(place_of(locals, index));
        loop.operands.push_back(read_place(locals, index, header.statement));
    }
    loop.invariants = static_cast<std::uint32_t>(invariants.size());
    for (Expr& invariant : invariants) {
        loop.operands.push_back(std::move(invariant));
    }
    loop.measures = static_cast<std::uint32_t>(measures.size());
    for (Expr& measure : measures) {
        loop.operands.push_back(std::move(measure));
    }
    loop.operands.push_back(std::move(head));

    Expr lowered;
    lowered.type = result_type;
    lowered.location = location;
    lowered.node = std::move(loop);
    return lowered;
}

// The end of an iteration: the increment, then the next iteration with
// each carried local at the version it holds here. A local the loop does
// not carry must still hold its head version, or the scan that decided
// what the loop carries missed a write.
std::optional<Expr> BodyLowering::end_iteration(const LoopFrame& frame, bool after_increment, const Locals& locals,
                                                unsigned depth) {
    if (frame.range != nullptr && !after_increment) {
        return advance_range(frame, locals, depth);
    }
    if (frame.increment && !after_increment) {
        Continuation incremented;
        incremented.iteration = &frame;
        incremented.after_increment = true;
        return lower_statement(*frame.increment, incremented, locals, depth + 1);
    }
    if (locals.size() < frame.head.size()) {
        return reject("a loop's locals went out of step with its head");
    }
    Iterate next;
    next.loop = frame.id;
    for (std::size_t index = 0; index < frame.head.size(); ++index) {
        if (clang_equalCursors(locals[index].declaration, frame.head[index].declaration) == 0) {
            return reject("a loop's locals went out of step with its head");
        }
        const bool carried = std::ranges::find(frame.carried, index) != frame.carried.end();
        if (!carried && locals[index].version != frame.head[index].version) {
            return reject("'" + take(clang_getCursorSpelling(locals[index].declaration)) +
                          "' is written inside a loop in a way this implementation does not track");
        }
        if (carried) {
            next.operands.push_back(read_place(locals, index, frame.statement));
        }
    }
    Expr iterated;
    iterated.type = result_type;
    iterated.location = presumed_location(clang_getCursorLocation(frame.statement));
    iterated.node = std::move(next);
    if (!frame.condition_last) {
        return iterated;
    }
    // A `do` loop decides here, where its body ends or a `continue` leaves
    // it, whether another iteration begins; when not, what follows the loop
    // runs under the versions current here and outside the loop.
    const Branch again = [&](const Locals&) -> std::optional<Expr> {
        return iterated;
    };
    const Branch leave = [&](const Locals& state) {
        const std::vector<const LoopFrame*> inside = frames;
        const std::vector<const SwitchFrame*> switches = switch_frames;
        frames.resize(frame.frames_outside);
        leave_switches_inside(frame);
        std::optional<Expr> after = lower_statements(*frame.exit, state, depth + 1);
        frames = inside;
        switch_frames = switches;
        return after;
    };
    return lower_condition(frame.condition, again, leave, locals, depth + 1);
}

// `break` continues with what follows the innermost loop or switch, under
// the versions current here, and outside it. A switch is the innermost when
// no loop was entered after it.
std::optional<Expr> BodyLowering::lower_break(const Locals& locals, unsigned depth) {
    if (!switch_frames.empty() && switch_frames.back()->loops_outside == frames.size()) {
        return leave_switch(*switch_frames.back(), locals, depth);
    }
    if (frames.empty()) {
        return reject("'break' outside a modeled loop or switch");
    }
    // No switch entered inside this loop is still open here: a `break`
    // inside one belongs to it.
    const LoopFrame& frame = *frames.back();
    const std::vector<const LoopFrame*> inside = frames;
    frames.resize(frame.frames_outside);
    std::optional<Expr> after = lower_statements(*frame.exit, locals, depth + 1);
    frames = inside;
    return after;
}

// Forgets the switches entered inside `frame`'s loop, for what follows the
// loop, where a `break` cannot belong to them. A `continue` in such a
// switch reaches the end of an iteration with the switch still open.
void BodyLowering::leave_switches_inside(const LoopFrame& frame) {
    while (!switch_frames.empty() && switch_frames.back()->loops_outside > frame.frames_outside) {
        switch_frames.pop_back();
    }
}

} // namespace cppl::clangbridge::detail
