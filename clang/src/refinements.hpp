#pragma once

#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"

#include <clang-c/Index.h>
#include <cstddef>
#include <expected>
#include <string>
#include <vector>

// The refinements a written type names (refinements.cpp, where each is
// documented).
namespace cppl::clangbridge::detail {

// Why the refinements a written type names could not be read, and what kind of
// failure that is. Where the failure is reported as a diagnostic of its own,
// its category says which; elsewhere the message is a reason a construct is
// not modeled.
struct RefinementFailure {
    Category category = Category::Internal;
    std::string message;
};

std::expected<std::vector<Refinement>, RefinementFailure> refinements_of(
    CXCursor declared, CXType written, const std::vector<Selection::Refinement>& known);

CXType written_element_type(CXType written);

std::expected<Type, std::string> sequence_element(CXCursor declared, CXType written,
                                                  const std::vector<Selection::Refinement>* known);

std::size_t physical_offset(CXCursor cursor);

} // namespace cppl::clangbridge::detail
