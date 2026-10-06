#pragma once

#include "cppl/clang/ast.hpp"
#include "cppl/source/projection.hpp"
#include "places.hpp"
#include "signature.hpp"

#include <clang-c/Index.h>
#include <expected>
#include <string>

// A contract clause's proposition and capabilities (formal.cpp, where each is
// documented).
namespace cppl::clangbridge::detail {

bool mixes_capabilities(const source::ProjectionShape& shape, unsigned depth = 0);

void extract_formal(Function& function, CXCursor cursor, const Signature& signature,
                    const source::ProjectionShape& shape);

std::expected<Expr, std::string> build_invariant(CXCursor cursor, const source::ProjectionShape& shape,
                                                 const Signature& signature, const Locals& head);

} // namespace cppl::clangbridge::detail
