#pragma once

#include "cppl/clang/ast.hpp"
#include "cppl/source/projection.hpp"
#include "signature.hpp"

#include <clang-c/Index.h>

// A contract clause's proposition and capabilities (formal.cpp, where each is
// documented).
namespace cppl::clangbridge::detail {

bool mixes_capabilities(const source::ProjectionShape& shape, unsigned depth = 0);

void extract_formal(Function& function, CXCursor cursor, const Signature& signature,
                    const source::ProjectionShape& shape);

} // namespace cppl::clangbridge::detail
