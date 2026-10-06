#pragma once

#include "cppl/clang/ast.hpp"
#include "signature.hpp"

#include <clang-c/Index.h>
#include <expected>
#include <string>

// The default arguments a call relies on (default_arguments.cpp, where each is
// documented).
namespace cppl::clangbridge::detail {

bool is_default_argument(CXCursor argument);

std::string default_owner(CXCursor callee, unsigned index);

std::expected<CXCursor, std::string> default_argument_of(CXCursor callee, unsigned index);

Expr lower_default_argument(CXCursor call, CXCursor callee, unsigned index, CXCursor argument,
                            const Signature& signature, unsigned depth);

} // namespace cppl::clangbridge::detail
