#pragma once

#include "cppl/frontend/syntax.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace cppl::lsp {

// The one semantic token type this server reports. A token's type is its
// index in the legend the server advertises, so every token here is type 0.
inline constexpr std::string_view kKeywordTokenType = "keyword";

// The keyword of every proof statement the recognizer read, in `text`'s own
// coordinates, as LSP semantic tokens: five integers per token, each position
// relative to the one before (LSP: `SemanticTokens.data`).
//
// `exact h;`, `assume h : P;` and `contradiction name;` are spelled like C++
// declarations, so the editors' TextMate grammar cannot tell them from one and
// leaves them uncolored. Only the recognizer knows which ones are proof
// statements, so these tokens come from what it recorded, never from spelling.
//
// A `contradiction` statement in a verified body is a claim only when no other
// part of the translation unit, headers included, uses the word (SPEC.md
// WORD-002). `syntax` comes from the unpreprocessed buffer, which cannot see
// its headers, so such a claim is colored only when
// `path_claims_recognized` says the compile of the preprocessed unit recognized
// its claims too. A `cases` or `decompose` statement in a verified body is a
// case split on the same terms (SPEC.md CASE-017), so it and the keywords of
// its arms are colored only when `path_splits_recognized` says so.
[[nodiscard]] std::vector<std::uint32_t> proof_keyword_tokens(const frontend::Syntax& syntax, std::string_view text,
                                                              bool path_claims_recognized,
                                                              bool path_splits_recognized = false);

} // namespace cppl::lsp
