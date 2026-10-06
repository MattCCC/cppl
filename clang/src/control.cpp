#include "cppl/clang/ast.hpp"
#include "cppl/source/location.hpp"
#include "expressions.hpp"
#include "ghost.hpp"
#include "lowering.hpp"
#include "places.hpp"
#include "sequences.hpp"
#include "signature.hpp"
#include "statements.hpp"
#include "types.hpp"
#include "unsafe.hpp"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// Statements, conditions and `if` in a verified body (SPEC.md 12.8, 12.7, C++
// [stmt.if]): the rest of a body lowered statement by statement, each statement
// form, and the routes a condition selects between, `&&`, `||` and `!`
// elaborated into the branches C++ gives them.
namespace cppl::clangbridge::detail {

using bridge::children_of;
using bridge::presumed_location;
using bridge::strip_parens;
using bridge::take;

namespace {

// A condition's operators nest, and each `&&`/`||` places its second operand on
// a further route, so elaboration is bounded as expression depth is.
constexpr unsigned kMaxConditionDepth = 64;

std::string statement_name(CXCursorKind kind) {
    switch (kind) {
        case CXCursor_SwitchStmt:
            return "a 'switch' statement";
        case CXCursor_GotoStmt:
        case CXCursor_IndirectGotoStmt:
            return "a 'goto' statement";
        case CXCursor_LabelStmt:
            return "a label";
        case CXCursor_CXXTryStmt:
            return "a 'try' block (exceptions are not modeled)";
        case CXCursor_CXXThrowExpr:
            return "a 'throw' (exceptions are not modeled)";
        case CXCursor_NullStmt:
            return "an empty statement";
        case CXCursor_GCCAsmStmt:
        case CXCursor_MSAsmStmt:
            return "inline assembly";
        default:
            return "'" + take(clang_getCursorKindSpelling(kind)) + "'";
    }
}

// Whether control leaves the function rather than reaching what follows. It
// decides reachability only; what each statement means is decided by the
// lowering below.
bool terminates(CXCursor statement, unsigned depth) {
    if (depth > kMaxExpressionDepth) {
        return false;
    }
    const CXCursorKind kind = clang_getCursorKind(statement);
    if (kind == CXCursor_ReturnStmt || kind == CXCursor_BreakStmt || kind == CXCursor_ContinueStmt) {
        return true;
    }
    if (kind == CXCursor_CompoundStmt) {
        const std::vector<CXCursor> nested = children_of(statement);
        return std::ranges::any_of(nested, [depth](CXCursor child) { return terminates(child, depth + 1); });
    }
    if (kind == CXCursor_IfStmt) {
        const std::vector<CXCursor> parts = children_of(statement);
        return parts.size() == 3 && terminates(parts[1], depth + 1) && terminates(parts[2], depth + 1);
    }
    return false;
}

} // namespace

std::string unmodeled_statement(const std::string& found) {
    return "only if/else, while and for loops, blocks, local declarations, assignments, and return statements are "
           "modeled; found " +
           found;
}

std::optional<Expr> BodyLowering::lower_statements(const Continuation& from, const Locals& locals, unsigned depth) {
    // Each statement lowers the rest of the body inside itself, so this
    // bounds the statements on one path as well as their nesting.
    if (depth > kMaxExpressionDepth) {
        return reject("more than " + std::to_string(kMaxExpressionDepth) +
                      " nested or consecutive statements on one path are not modeled");
    }
    if (from.header != nullptr) {
        return lower_loop(*from.header, locals, depth + 1);
    }
    if (from.iteration != nullptr) {
        return end_iteration(*from.iteration, from.after_increment, locals, depth + 1);
    }
    if (from.dispatch != nullptr) {
        return lower_switch_dispatch(*from.dispatch, locals, depth + 1);
    }
    if (from.left != nullptr) {
        return leave_switch(*from.left, locals, depth + 1);
    }
    if (from.branch != nullptr) {
        return lower_branch(*from.branch, locals, depth + 1);
    }
    if (from.index == from.statements->size()) {
        if (from.outer == nullptr) {
            if (result_type.kind == TypeKind::Void) {
                const CXCursor at = clang_getNullCursor();
                return completed(void_value(at), locals, at);
            }
            return reject("every path must return a value");
        }
        return lower_statements(*from.outer, locals, depth + 1);
    }
    const CXCursor statement = (*from.statements)[from.index];
    if (const std::optional<std::string> marker = contradiction_marker(statement)) {
        return lower_contradiction(*marker, *from.statements, from.index, locals);
    }
    if (const std::optional<std::string> marker = split_marker(*from.statements, from.index)) {
        return lower_split(*marker, from, locals, depth);
    }
    if (ghost_marker_of(statement, invariant_prefix)) {
        return lower_ghost(from, locals, depth);
    }
    Continuation next{from.outer, from.statements, from.index + 1};
    next.labels = from.labels;
    // An unsafe block says for itself why a way out of it is refused. A
    // statement a switch's label leads into is reached by that label.
    const bool labelled = next.labels != nullptr && std::ranges::contains(*next.labels, next.index);
    if (next.index != from.statements->size() && !labelled &&
        !unsafe_marker_of(statement, invariant_prefix).has_value() && terminates(statement, 0)) {
        return reject("unreachable trailing statements are not modeled");
    }
    return lower_statement(statement, next, locals, depth);
}

// Lower one statement, then bind every dereference place it formed.
//
// The binding wraps the whole statement's value, so each pointee has an
// entry value before anything reads it. Doing it here rather than in each
// statement form is what keeps a dereference from needing a lowering rule
// of its own (RFC 0014 §17 step 6).
std::optional<Expr> BodyLowering::lower_statement(CXCursor statement, const Continuation& next, const Locals& locals,
                                                  unsigned depth) {
    std::vector<Local> enclosing;
    enclosing.swap(formed_derefs);
    std::optional<Expr> lowered = lower_statement_form(statement, next, locals, depth);
    if (lowered) {
        lowered = bind_formed_derefs(std::move(*lowered), statement);
    }
    formed_derefs = std::move(enclosing);
    return lowered;
}

std::optional<Expr> BodyLowering::lower_statement_form(CXCursor statement, const Continuation& next,
                                                       const Locals& locals, unsigned depth) {
    const CXCursorKind kind = clang_getCursorKind(statement);
    if (const std::optional<CXCursor> marker = unsafe_marker_of(statement, invariant_prefix)) {
        return lower_unsafe(statement, *marker, next, locals, depth);
    }
    if (kind == CXCursor_CompoundStmt) {
        const std::vector<CXCursor> nested = children_of(statement);
        return lower_statements(Continuation{&next, &nested, 0}, locals, depth + 1);
    }
    if (const auto selected = selection_to_split(statement))
        return lower_selected_statement(statement, *selected, next, locals, depth);
    if (kind == CXCursor_CallExpr)
        return lower_call(statement, next, locals, depth);
    // A container mutator whose argument is a temporary stands inside the
    // node Clang adds to destroy that temporary at the statement's end.
    if (kind == CXCursor_UnexposedExpr) {
        const CXCursor inner = strip_parens(statement);
        if (clang_getCursorKind(inner) == CXCursor_CallExpr && sequence_call(inner).has_value()) {
            return lower_call(inner, next, locals, depth);
        }
        // A call whose temporaries are destroyed without running any code
        // of the program's is the call it holds: the cleanup has no effect.
        if (clang_getCursorKind(inner) == CXCursor_CallExpr && temporaries_destroy_silently(statement)) {
            return lower_call(inner, next, locals, depth);
        }
        if (clang_getCursorKind(inner) == CXCursor_CallExpr) {
            return reject("a call statement creates a temporary whose destruction at the statement's end runs a "
                          "user-provided destructor, which is not modeled (SPEC.md STDMODEL-023)");
        }
    }
    if (kind == CXCursor_NullStmt || is_fallthrough(statement))
        return lower_statements(next, locals, depth + 1);
    // `a, b;` as a statement, and as a `for` increment, runs `a` and then
    // `b`, each as a statement of its own (C++ [expr.comma]); `a, b, c` is
    // `(a, b), c`. A comma inside another expression is not this.
    if (kind == CXCursor_BinaryOperator && clang_getCursorBinaryOperatorKind(statement) == CXBinaryOperator_Comma) {
        const std::vector<CXCursor> operands = children_of(statement);
        if (operands.size() != 2) {
            return reject("the operands of this comma operator could not be resolved");
        }
        const std::vector<CXCursor> right{operands[1]};
        const Continuation then{&next, &right, 0};
        return lower_statement(operands[0], then, locals, depth + 1);
    }
    if (kind == CXCursor_SwitchStmt) {
        return lower_switch(statement, next, locals, depth);
    }
    if (kind == CXCursor_ReturnStmt) {
        const std::vector<CXCursor> returned = children_of(statement);
        if (returned.empty() && result_type.kind == TypeKind::Void)
            return completed(void_value(statement), locals, statement);
        if (returned.size() != 1)
            return reject("a return requires one value");
        return lower_returned(returned.front(), statement, locals, depth);
    }
    if (kind == CXCursor_DeclStmt) {
        // The projector puts one declaration in a templated body to make
        // C++ instantiate that specialization's contract probes with it.
        // It names a probe and computes nothing, so it is not a statement
        // of the program being verified and is stepped over rather than
        // modeled (SPEC.md TEMPLATE-001).
        if (is_instantiation_marker(statement)) {
            return lower_statements(next, locals, depth + 1);
        }
        return lower_declaration(children_of(statement), 0, next, locals, depth);
    }
    if (kind == CXCursor_BinaryOperator && clang_getCursorBinaryOperatorKind(statement) == CXBinaryOperator_Assign) {
        return lower_assignment(statement, next, locals, depth);
    }
    if (kind == CXCursor_CompoundAssignOperator || kind == CXCursor_UnaryOperator) {
        return lower_update(statement, next, locals, depth);
    }
    const std::vector<CXCursor> parts = children_of(statement);
    if (kind == CXCursor_IfStmt) {
        return lower_if(statement, next, locals, depth);
    }
    // The condition variable of an `if` or a `switch`, which no other
    // statement list holds.
    if (kind == CXCursor_VarDecl) {
        return lower_declaration(std::vector<CXCursor>{statement}, 0, next, locals, depth);
    }
    if (kind == CXCursor_WhileStmt) {
        if (parts.size() != 2 || clang_isExpression(clang_getCursorKind(parts[0])) == 0) {
            return reject("a while loop whose condition declares a variable is not modeled");
        }
        const LoopHeader header{statement, parts[0], parts[1], std::nullopt, &next};
        return lower_loop(header, locals, depth);
    }
    if (kind == CXCursor_ForStmt) {
        return lower_for(statement, next, locals, depth);
    }
    if (kind == CXCursor_BreakStmt) {
        return lower_break(locals, depth);
    }
    if (kind == CXCursor_ContinueStmt) {
        if (frames.empty()) {
            return reject("'continue' outside a modeled loop");
        }
        return end_iteration(*frames.back(), false, locals, depth);
    }
    if (kind == CXCursor_DoStmt) {
        if (parts.size() != 2 || clang_isExpression(clang_getCursorKind(parts[1])) == 0) {
            return reject("the parts of this do loop could not be resolved");
        }
        const LoopHeader header{statement, parts[1], parts[0], std::nullopt, &next, true};
        return lower_loop(header, locals, depth);
    }
    // A label names the statement it labels and does nothing itself; a
    // `goto` to it is refused where the `goto` stands.
    if (kind == CXCursor_LabelStmt) {
        if (parts.size() != 1 || clang_isStatement(clang_getCursorKind(parts[0])) == 0) {
            return reject("the statement this label names could not be resolved");
        }
        return lower_statement(parts[0], next, locals, depth);
    }
    if (kind == CXCursor_CXXForRangeStmt) {
        return lower_range_for(statement, next, locals, depth);
    }
    return reject(unmodeled_statement(statement_name(kind)));
}

std::optional<Expr> BodyLowering::lower_for(CXCursor statement, const Continuation& next, const Locals& locals,
                                            unsigned depth) {
    const std::optional<ForParts> parts = for_parts(statement);
    if (!parts) {
        return reject("the parts of this for loop could not be resolved");
    }
    if (parts->condition && clang_isExpression(clang_getCursorKind(*parts->condition)) == 0) {
        return reject("a for loop whose condition declares a variable is not modeled");
    }
    // A `for` without a condition runs until a `break` or a `return` leaves
    // it (SPEC.md LOOP-001).
    const LoopHeader header{statement, parts->condition.value_or(clang_getNullCursor()), parts->body, parts->increment,
                            &next};
    if (!parts->initialization) {
        return lower_loop(header, locals, depth);
    }
    // The initialization runs once, before the loop, with the loop as what
    // follows it.
    Continuation entered;
    entered.header = &header;
    return lower_statement(*parts->initialization, entered, locals, depth);
}

// Elaborate an `if` condition into the routes it selects between.
//
// `&&` and `||` state a proposition, and a proposition is not a value: the
// core computes no Boolean from one (SPEC.md 12.7). They are not lowered as
// values here either. They are elaborated into the branch structure C++
// already gives them, which is what makes short-circuit evaluation exact
// rather than approximated:
//
//     if (A && B) T else F   ==>   if (A) { if (B) T else F } else F
//     if (A || B) T else F   ==>   if (A) T else { if (B) T else F }
//     if (!A)     T else F   ==>   if (A) F else T
//
// `B` appears only under the route on which C++ evaluates it, so no route
// can state a fact about an operand that did not execute on it. The false
// route of `A && B` is the union of `!A` and `A && !B`; it is represented as
// those two routes, never as a single route supposing both operands false.
// Nesting recurses, so each operand is itself elaborated the same way.
std::optional<Expr> BodyLowering::lower_condition(CXCursor condition, const Branch& when_true, const Branch& when_false,
                                                  const Locals& locals, unsigned depth) {
    if (depth > kMaxConditionDepth) {
        return reject("this condition nests more deeply than " + std::to_string(kMaxConditionDepth) + " operators");
    }
    const enum CXCursorKind kind = clang_getCursorKind(condition);
    if (kind == CXCursor_ParenExpr) {
        const auto inner = children_of(condition);
        if (inner.size() == 1)
            return lower_condition(inner[0], when_true, when_false, locals, depth + 1);
    }
    if (kind == CXCursor_UnaryOperator && clang_getCursorUnaryOperatorKind(condition) == CXUnaryOperator_LNot) {
        const auto operands = children_of(condition);
        if (operands.size() == 1)
            return lower_condition(operands[0], when_false, when_true, locals, depth + 1);
    }
    if (kind == CXCursor_BinaryOperator) {
        const enum CXBinaryOperatorKind op = clang_getCursorBinaryOperatorKind(condition);
        const auto operands = children_of(condition);
        if ((op == CXBinaryOperator_LAnd || op == CXBinaryOperator_LOr) && operands.size() == 2) {
            const bool conjunction = op == CXBinaryOperator_LAnd;
            // The second operand is evaluated only on the route the first
            // operand's outcome leads to, which is where it is placed.
            const Branch rest = [&](const Locals& state) -> std::optional<Expr> {
                return lower_condition(operands[1], when_true, when_false, state, depth + 1);
            };
            return lower_condition(operands[0], conjunction ? rest : when_true, conjunction ? when_false : rest, locals,
                                   depth + 1);
        }
    }
    // What the leaf reads through a subscript or a pointer is formed where the
    // leaf is evaluated, and owes its bound or capability on the routes that
    // reach it and nowhere else.
    return forming(condition, [&]() -> std::optional<Expr> {
        Locals state = locals;
        if (!signature.clause && !form_places(condition, state))
            return std::nullopt;
        Expr value = runtime_value(build_expression(condition, signature, state, 0), signature.clause);
        std::optional<Expr> taken = when_true(state);
        std::optional<Expr> untaken = taken ? when_false(state) : std::nullopt;
        if (!taken || !untaken)
            return std::nullopt;
        if (return_paths(*taken) + return_paths(*untaken) > kMaxReturnPaths)
            return reject("more than " + std::to_string(kMaxReturnPaths) + " return paths are not modeled");
        Expr result;
        result.type = taken->type;
        result.location = value.location;
        result.node = Conditional{{std::move(value), std::move(*taken), std::move(*untaken)}};
        return result;
    });
}

// An `if` statement (C++ [stmt.if]). An init-statement runs first, in a
// scope enclosing the whole statement, so what it declares is visible in
// the condition and in both branches and ends after them; a condition
// variable is a local its initializer initializes, and the condition reads
// it. Which child is which is decided by where each stands against the
// head's parentheses: the parts written in them come first, and what
// follows them is the branches.
std::optional<Expr> BodyLowering::lower_if(CXCursor statement, const Continuation& next, const Locals& locals,
                                           unsigned depth) {
    const std::optional<SelectionHead> head = selection_head(statement);
    if (!head.has_value()) {
        return reject("the head of this 'if' statement could not be read");
    }
    if (head->immediate) {
        return reject("an 'if consteval' statement is not modeled: which branch runs depends on whether the "
                      "evaluation is a constant one, and a contract describes the function as it runs");
    }
    const std::vector<CXCursor> parts = children_of(statement);
    std::size_t inside = 0;
    while (inside < parts.size() && before(start_of(parts[inside]), head->file, head->close)) {
        ++inside;
    }
    const std::size_t branches = parts.size() - inside;
    if (inside == 0 || (branches != 1 && branches != 2) || !stands_at(start_of(parts[0]), head->file, head->first)) {
        return reject("the parts of this 'if' statement could not be resolved");
    }
    // The parts in the parentheses: the init-statement, which ends before
    // the head's `;`, then a condition variable, then the condition.
    std::vector<CXCursor> prefix;
    std::size_t condition = 0;
    if (head->separator.has_value() && before(start_of(parts[0]), head->file, *head->separator)) {
        prefix.push_back(parts[0]);
        condition = 1;
    }
    if (condition < inside && clang_getCursorKind(parts[condition]) == CXCursor_VarDecl) {
        prefix.push_back(parts[condition]);
        ++condition;
    }
    // An init-statement no `;` in the head shows, written through a macro,
    // is not read as the condition.
    if (condition + 1 != inside || clang_isExpression(clang_getCursorKind(parts[condition])) == 0) {
        return reject("the parts of this 'if' statement could not be resolved");
    }
    const IfHeader header{statement,
                          std::vector<CXCursor>(parts.begin() + static_cast<std::ptrdiff_t>(condition), parts.end()),
                          &next, head->constant};
    if (prefix.empty()) {
        return lower_branch(header, locals, depth);
    }
    Continuation decided;
    decided.branch = &header;
    return lower_statements(Continuation{&decided, &prefix, 0}, locals, depth + 1);
}

// The branches of an `if`. Those of `if constexpr` are selected by a
// constant condition Clang evaluates, and only the selected one runs: in a
// template the other is not even instantiated.
std::optional<Expr> BodyLowering::lower_branch(const IfHeader& header, const Locals& locals, unsigned depth) {
    if (!header.constant) {
        return lower_branch(header.statement, header.parts, *header.exit, locals, depth);
    }
    CXEvalResult evaluated = clang_Cursor_Evaluate(header.parts[0]);
    const bool known = evaluated != nullptr && clang_EvalResult_getKind(evaluated) == CXEval_Int;
    const bool holds = known && clang_EvalResult_getAsLongLong(evaluated) != 0;
    if (evaluated != nullptr) {
        clang_EvalResult_dispose(evaluated);
    }
    if (!known) {
        return reject("the condition of this 'if constexpr' is not a constant Clang evaluates");
    }
    if (holds) {
        return lower_statement(header.parts[1], *header.exit, locals, depth + 1);
    }
    if (header.parts.size() == 3) {
        return lower_statement(header.parts[2], *header.exit, locals, depth + 1);
    }
    return lower_statements(*header.exit, locals, depth + 1);
}

std::optional<Expr> BodyLowering::lower_branch(CXCursor statement, const std::vector<CXCursor>& parts,
                                               const Continuation& next, const Locals& locals, unsigned depth) {
    const Branch when_true = [&](const Locals& state) -> std::optional<Expr> {
        return lower_statement(parts[1], next, state, depth + 1);
    };
    const Branch when_false = [&](const Locals& state) -> std::optional<Expr> {
        return parts.size() == 3 ? lower_statement(parts[2], next, state, depth + 1)
                                 : lower_statements(next, state, depth + 1);
    };
    std::optional<Expr> result = lower_condition(parts[0], when_true, when_false, locals, depth);
    if (!result)
        return std::nullopt;
    result->location = presumed_location(clang_getCursorLocation(statement));
    return result;
}

} // namespace cppl::clangbridge::detail
