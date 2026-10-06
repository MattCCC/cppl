#pragma once

// What the bridge reads of a statement's shape before it lowers it: the head
// of an `if` or a `switch`, the parts of a `for`, a switch's labels,
// `[[fallthrough]];`, the arms of a returned value a condition selects, and the
// value of a logical operator a runtime expression uses (C++ [stmt.select],
// [stmt.switch], [stmt.for], [expr.cond], [expr.log.and], [expr.log.or]).
// Nothing here decides what a statement means; the body lowering
// (lowering.hpp) lowers it from what this reads.

#include "cppl/clang/ast.hpp"

#include <clang-c/CXFile.h>
#include <clang-c/Index.h>
#include <optional>
#include <vector>

namespace cppl::clangbridge::detail {

// Where a location is in the file its translation unit reads, so the head of
// a statement and its parts can be compared.
struct FilePosition {
    CXFile file = nullptr;
    unsigned offset = 0;
};

[[nodiscard]] FilePosition start_of(CXCursor cursor);

// Whether `position` stands in `file` before `offset`.
[[nodiscard]] bool before(const FilePosition& position, CXFile file, unsigned offset);

// Whether `position` stands in `file` at `offset`.
[[nodiscard]] bool stands_at(const FilePosition& position, CXFile file, unsigned offset);

// The head of an `if` or a `switch`, read from its tokens (C++ [stmt.select]):
// `if constexpr`, `if consteval`, and in the parentheses after the keyword,
// where they open and close and the `;` that ends an init-statement directly
// inside them. libclang does not expose a switch's init-statement as a child
// at all, and lists an `if`'s where the condition otherwise stands, so the
// head is what says which part is which.
struct SelectionHead {
    bool constant = false;  // `if constexpr`
    bool immediate = false; // `if consteval` or `if !consteval`, which has no parentheses
    CXFile file = nullptr;
    unsigned first = 0; // where the first token inside the parentheses stands
    std::optional<unsigned> separator;
    unsigned close = 0;
};

// Nothing when the head cannot be read, which the caller refuses rather than
// guessing.
[[nodiscard]] std::optional<SelectionHead> selection_head(CXCursor statement);

[[nodiscard]] bool is_switch_label(CXCursor statement);

// Whether a `case` or `default` label of the switch being lowered stands
// somewhere in `root` (C++ [stmt.switch]): a nested switch and a lambda own
// the labels inside them. Such a label is a way into the middle of `root`
// that only lowering `root` from its start would never take.
[[nodiscard]] bool holds_switch_label(CXCursor root, unsigned depth = 0);

// Whether `statement` is `[[fallthrough]];`, an empty statement that says a
// label is reached by falling into it (C++ [dcl.attr.fallthrough]). Any other
// attribute on an empty statement, such as `[[assume(e)]]`, is not this one.
[[nodiscard]] bool is_fallthrough(CXCursor statement);

// The parts of a `for` header. libclang omits an empty part instead of marking
// it, so each part is placed by where it starts relative to the header's two
// top-level semicolons.
struct ForParts {
    std::optional<CXCursor> initialization;
    std::optional<CXCursor> condition;
    std::optional<CXCursor> increment;
    CXCursor body = clang_getNullCursor();
};

[[nodiscard]] std::optional<ForParts> for_parts(CXCursor statement);

// The arms of a returned scalar value that a condition selects between, seen
// through the parentheses and conversions that keep its value: `c ? a : b`;
// `a && b`, which is `a ? b : false`; and `a || b`, which is `a ? true : b`
// (C++ [expr.cond], [expr.log.and], [expr.log.or]). An arm with no cursor is
// the constant, which no expression of the program writes.
struct SelectedValue {
    CXCursor condition = clang_getNullCursor();
    std::optional<CXCursor> when_true;
    std::optional<CXCursor> when_false;
    Expr constant;
    CXCursor selection = clang_getNullCursor(); // the `?:`, `&&` or `||` itself
};

[[nodiscard]] std::optional<SelectedValue> selected_value(CXCursor value);

// The selected value a declaration of one local or an assignment computes,
// when an arm of it reads storage through a subscript or a pointer: the
// statement is then lowered once on each route its condition selects, with
// the arm that route evaluates in place of the selection (`ChosenArm`), so
// what the arm reads owes its bound or capability on that route alone.
[[nodiscard]] std::optional<SelectedValue> selected_reading(CXCursor statement);

// The arm a route evaluates in place of `selection`: a cursor, or else the
// constant no expression of the program writes.
struct ChosenArm {
    CXCursor selection = clang_getNullCursor();
    std::optional<CXCursor> arm;
    Expr constant;
};

// The arm `chosen` holds for `selection` on this route, if any.
[[nodiscard]] const ChosenArm* chosen_for(const std::vector<ChosenArm>& chosen, CXCursor selection);

// A value a body's code computes, with every logical operator it uses as a
// value, `a && b` and `a || b`, written as the conditional C++ evaluates:
// `a ? b : false` and `a ? true : b` ([expr.log.and], [expr.log.or]). The
// second operand is then an arm the first selects, so an operation in it owes
// its conditions only where it is evaluated, as an arm of `?:` does. A
// `clause`, or a definition the formal core reads, keeps them as the
// connectives a specification states (SPEC.md 7.6, 7.8). A condition a path
// is taken on never reaches here as one value: it is split into the routes it
// selects first.
[[nodiscard]] Expr runtime_value(Expr value, bool clause);

} // namespace cppl::clangbridge::detail
