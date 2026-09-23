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
// well it matches what has been typed and then by Clang's own ranking.
//
// C++L is the compiler's: what its recognizer says may be written there
// (frontend::admissible_at), never what this server reads from the tokens
// itself. A declaration where one may begin at namespace scope, a statement
// where one may begin in a proof body, the evidence a statement may name after
// its keyword, the clauses a declaration may still take. None of it resolves
// anything: a suggestion the compiler would reject is only a suggestion, and
// the compiler still decides.

// Clang's completions, filtered by `prefix` -- what has been typed of the name
// -- and ranked. A name the projection generated is never offered, nor is a
// reserved name (`__x`) unless the prefix starts with an underscore.
[[nodiscard]] CompletionList cpp_completions(std::vector<clangbridge::Completion> completions, std::string_view prefix,
                                             bool snippets);

// C++L's own completions where `prefix` is being typed from byte `start`.
// `draft` is the recognizer's draft (frontend::RecognitionMode::Draft) of the
// text lexed as `tokens`, and `scope` is what Clang says encloses `start`.
[[nodiscard]] std::vector<CompletionItem> cppl_completions(const frontend::TokenStream& tokens,
                                                           const frontend::Syntax& draft, std::size_t start,
                                                           std::string_view prefix, clangbridge::Scope scope,
                                                           bool snippets);

} // namespace cppl::lsp
