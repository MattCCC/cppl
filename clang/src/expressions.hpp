#pragma once

#include "cppl/clang/ast.hpp"
#include "places.hpp"
#include "signature.hpp"

#include <clang-c/Index.h>

// The lowering of an expression of a verified body or a clause into a term
// (SPEC.md 12.8), documented where it is defined.
namespace cppl::clangbridge::detail {

Expr build_expression(CXCursor cursor, const Signature& signature, const Locals& locals, unsigned depth,
                      bool sequenced_call = false);

} // namespace cppl::clangbridge::detail
