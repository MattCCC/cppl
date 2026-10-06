#pragma once

#include "places.hpp"

#include <clang-c/Index.h>
#include <cstddef>
#include <optional>
#include <unordered_set>
#include <vector>

// The write and escape scans (write_scan.cpp, where each is documented).
namespace cppl::clangbridge::detail {

// Marks each local in `locals` that the statement or expression writes by
// assignment, compound assignment, increment or decrement. Any other way of
// writing a local is refused when the body is lowered, and a local this misses
// is caught at the end of every iteration, so the scan only has to be complete
// for the writes the lowering accepts.
struct WriteScan {
    const Locals* locals;
    std::vector<bool>* written;
};

std::unordered_set<unsigned> escaped_locals(CXCursor body);

std::unordered_set<unsigned> unconfined_locals(CXCursor body);

std::optional<std::size_t> written_storage(CXCursor declaration, const Locals& locals, unsigned depth = 0);

void mark_writes(CXCursor root, const Locals& locals, std::vector<bool>& written);

} // namespace cppl::clangbridge::detail
