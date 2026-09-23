#pragma once
#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"

#include <expected>
#include <string>

namespace cppl::analysis {
struct Result {
    frontend::Projection projection;
    clangbridge::TranslationUnit unit;
};
// Resolves proof binding types through the same providers used by elaboration.
// Recovery passes supply only type information; only the final error-free AST
// may reach verification. The unit, not an individual arm, is the parse unit.
std::expected<Result, std::string> analyze(const frontend::TokenStream& stream, const frontend::Syntax& syntax,
                                           frontend::ProjectionOptions options, clangbridge::ParseRequest request);
} // namespace cppl::analysis
