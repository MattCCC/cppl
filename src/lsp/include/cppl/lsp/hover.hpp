#pragma once

#include "cppl/clang/editor.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace cppl::lsp {

// Markdown for what Clang knows about a C++ name. `declared_in` names where it
// is declared (`file:line`) when that is another file, and is empty otherwise.
[[nodiscard]] std::string describe_cpp(const clangbridge::Description& description, std::string_view declared_in);

// Markdown for the C++L declaration whose name is written at `name_offset` of a
// file -- a Law, a proof, a refinement type, a verified function or a name an
// `assume` binds -- presented as it is written, since what it means is the
// compiler's to decide. Nothing when no C++L declaration's name is there.
[[nodiscard]] std::optional<std::string> describe_cppl(const frontend::TokenStream& tokens,
                                                       const frontend::Syntax& syntax, std::string_view text,
                                                       std::size_t name_offset);

// Markdown for a name only generated code declares, which is what hover shows
// for it instead of the generated declaration: `result` in a contract and
// `self` in a refinement predicate. Nothing for any other name.
[[nodiscard]] std::optional<std::string> describe_implicit(const clangbridge::Description& description);

} // namespace cppl::lsp
