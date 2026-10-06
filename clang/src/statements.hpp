#pragma once

// What the bridge reads of a statement's shape before it lowers it: the head
// of an `if` or a `switch`, a switch's labels and `[[fallthrough]];` (C++
// [stmt.select], [stmt.switch]). Nothing here decides what a statement means;
// `bridge.cpp` lowers it from what this reads.

#include <clang-c/CXFile.h>
#include <clang-c/Index.h>
#include <optional>

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

} // namespace cppl::clangbridge::detail
