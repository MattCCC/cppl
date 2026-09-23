#pragma once

#include "cppl/clang/editor.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/lsp/protocol.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace cppl::lsp {

// Completion (tools/cppl-lsp/README.md, "Completion").
//
// Ordinary C++ is Clang's: what it would accept at the position, ranked by how
// well it matches what has been typed and then by Clang's own ranking. C++L's
// own words are offered only where the grammar admits them: a declaration at
// namespace scope, a statement at the start of a proof statement, the name of a
// proof or an assumption after `exact`, `apply`, `rewrite` or `contradiction`,
// a clause after a Law's, a proof's or a verified function's parameters. None
// of it resolves anything: a suggestion the compiler would reject is only a
// suggestion, and the compiler still decides.

// Clang's completions, filtered by `prefix` -- what has been typed of the name
// -- and ranked. A name the projection generated is never offered, nor is a
// reserved name (`__x`) unless the prefix starts with an underscore.
[[nodiscard]] CompletionList cpp_completions(std::vector<clangbridge::Completion> completions, std::string_view prefix,
                                             bool snippets);

// C++L's own completions at `offset` of a file as written, of which `prefix`
// ends at `offset`, where `scope` is what Clang says encloses it.
[[nodiscard]] std::vector<CompletionItem> cppl_completions(const frontend::TokenStream& tokens,
                                                           const frontend::Syntax& syntax, std::string_view text,
                                                           std::size_t offset, std::string_view prefix,
                                                           clangbridge::Scope scope, bool snippets);

} // namespace cppl::lsp
