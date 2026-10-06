#pragma once

#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/source/representation.hpp"
#include "places.hpp"

#include <clang-c/Index.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// How the Clang bridge models a resolved type, and how a diagnostic names what
// it reads (types.cpp, where each is documented).
namespace cppl::clangbridge::detail {

std::string describe_location(CXCursor at);

bool temporaries_destroy_silently(CXCursor statement);

source::RepresentationKind library_kind(CXCursor declaration);

bool is_standard_template(CXType type, std::string_view name);

std::int64_t enumerator_value(CXCursor enumerator, bool underlying_is_signed);

Type convert_type(CXType type, unsigned depth = 0, ReferenceModel references = ReferenceModel::Opaque,
                  const std::vector<Selection::Refinement>* known = nullptr);

std::string qualified_name_of(CXCursor cursor);

Expr unsupported_expression(CXCursor cursor, std::string reason);

} // namespace cppl::clangbridge::detail
