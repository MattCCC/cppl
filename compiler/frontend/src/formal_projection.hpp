#pragma once

#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/projection.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cppl::frontend::detail {

struct FormulaProjection {
    source::ProjectionShape shape;
    std::string expression;
    std::optional<std::string> failure;
};

std::string line_directive(std::uint32_t line, std::string_view file);
bool contains_formal_syntax(const TokenStream& stream, source::ByteSpan expression);
FormulaProjection project_formula(const TokenStream& stream, source::ByteSpan expression);

} // namespace cppl::frontend::detail
