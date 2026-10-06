#pragma once

#include "cppl/clang/ast.hpp"

#include <clang-c/Index.h>

// Conversions between modeled types (conversions.cpp, where each is
// documented).
namespace cppl::clangbridge::detail {

bool carries_refinement(const Type& type, unsigned depth = 0);

bool same_modeled_value(const Type& outer, const Type& inner);

bool integral(const Type& type);

Expr integral_conversion(Expr operand, Type type, CXCursor at, bool written);

Expr boolean_conversion(Expr operand, Type type, CXCursor at);

bool boolean_pair(const Type& from, const Type& to);

bool promoted_before_arithmetic(CXType type);

} // namespace cppl::clangbridge::detail
