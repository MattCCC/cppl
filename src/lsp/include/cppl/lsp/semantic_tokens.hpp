#pragma once

#include "cppl/elaboration/elaborate.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/source/location.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cppl::lsp {

// The token types this server reports, in the order its legend lists them: a
// token's type is its index here (LSP `SemanticTokensLegend.tokenTypes`).
enum class TokenType : std::uint8_t {
    Namespace,
    Type,
    Class,
    Enum,
    Struct,
    TypeParameter,
    Parameter,
    Variable,
    Property,
    EnumMember,
    Function,
    Method,
    Macro,
    Keyword,
};
inline constexpr auto kTokenTypes =
    std::to_array<std::string_view>({"namespace", "type", "class", "enum", "struct", "typeParameter", "parameter",
                                     "variable", "property", "enumMember", "function", "method", "macro", "keyword"});

// The modifiers, each a bit: bit `n` is `kTokenModifiers[n]`.
inline constexpr std::uint32_t kDeclaration = 1U << 0U;
inline constexpr std::uint32_t kReadonly = 1U << 1U;
inline constexpr std::uint32_t kStatic = 1U << 2U;
inline constexpr std::uint32_t kDeprecated = 1U << 3U;
inline constexpr std::uint32_t kDefaultLibrary = 1U << 4U;
inline constexpr auto kTokenModifiers =
    std::to_array<std::string_view>({"declaration", "readonly", "static", "deprecated", "defaultLibrary"});

struct SemanticToken {
    source::ByteSpan span;
    TokenType type = TokenType::Keyword;
    std::uint32_t modifiers = 0;
};

// C++L's own words and the names C++L declares, from what the recognizer
// recorded (ARCHITECTURE.md ARCH-LSP-006), never from spelling:
//
// - every C++L keyword: `law`, `trusted`, `proof`, `proves`, each clause's
//   keyword, `verified`, `pure`, `type` and `where` of a refinement type, each
//   proof statement's keyword, and `omit` and `by`;
// - the name each Law and proof declares, as a function, and each refinement
//   type's, as a type;
// - the name each `assume` binds, as a constant variable;
// - the name each proof statement uses, as the compile resolved it
//   (`resolved`): an assumption as a variable, a proof or a trusted Law as a
//   function. A name the compile did not resolve is left to the grammar.
//
// `exact h;`, `assume h : P;` and `contradiction name;` are spelled like C++
// declarations, so only the recognizer knows which ones are proof statements.
// A `contradiction` statement in a verified body is a claim only when no other
// part of the translation unit, headers included, uses the word (SPEC.md
// WORD-002). `syntax` comes from the unpreprocessed buffer, which cannot see
// its headers, so each word read on those terms is a token only where
// `recognized` says the compile of the preprocessed unit read it the same way.
struct UnitRecognition {
    bool path_claims = false; // `contradiction` in a verified body
    bool path_splits = false; // `cases` or `decompose` in one (SPEC.md CASE-017)
    bool unsafe = false;      // an unsafe block or declaration (SPEC.md 26)
    bool ghost = false;       // a ghost declaration (SPEC.md 25)
};

[[nodiscard]] std::vector<SemanticToken> cppl_tokens(const frontend::Syntax& syntax, UnitRecognition recognized,
                                                     const std::vector<elaboration::ResolvedName>& resolved = {});

// `tokens`, over `text`, as LSP semantic tokens: five integers per token, each
// position relative to the one before (LSP: `SemanticTokens.data`). They are
// sorted by position; where two overlap, the one listed first is kept.
[[nodiscard]] std::vector<std::uint32_t> encode(std::vector<SemanticToken> tokens, std::string_view text);

} // namespace cppl::lsp
