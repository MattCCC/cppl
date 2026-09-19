#pragma once

#include "cppl/source/location.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::frontend {

enum class TokenKind : std::uint8_t {
    Identifier,
    Number,
    StringLiteral,
    CharLiteral,
    Punctuator,
    EndOfFile,
};

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    source::ByteSpan span;
    std::string_view text;

    // The user-visible position this token came from, after the preprocessor's
    // line markers are applied.
    std::uint32_t file = 0;
    std::uint32_t line = 0;
    std::uint32_t column = 0;

    [[nodiscard]] bool is(TokenKind wanted) const noexcept {
        return kind == wanted;
    }
    [[nodiscard]] bool is_identifier(std::string_view name) const noexcept {
        return kind == TokenKind::Identifier && text == name;
    }
    [[nodiscard]] bool is_punctuator(std::string_view name) const noexcept {
        return kind == TokenKind::Punctuator && text == name;
    }
};

// A lexed buffer together with the file names its line markers referred to.
class TokenStream {
  public:
    TokenStream(std::string_view text, std::vector<Token> tokens, std::vector<std::string> files)
        : text_(text),
          tokens_(std::move(tokens)),
          files_(std::move(files)) {}

    // The scanned buffer. The stream does not own it; it stays valid only as
    // long as the buffer passed to lex() does.
    [[nodiscard]] std::string_view text() const noexcept {
        return text_;
    }

    [[nodiscard]] std::string_view spelling(source::ByteSpan span) const {
        return text_.substr(span.offset, span.length);
    }

    [[nodiscard]] const std::vector<Token>& tokens() const noexcept {
        return tokens_;
    }
    [[nodiscard]] const std::vector<std::string>& files() const noexcept {
        return files_;
    }

    [[nodiscard]] source::SourceLocation location_of(const Token& token) const;

  private:
    std::string_view text_;
    std::vector<Token> tokens_;
    std::vector<std::string> files_;
};

// Lexes preprocessed C++ text.
//
// The frontend runs after preprocessing (SPEC.md 3.2), so macros are already
// expanded and C++L constructs contributed by headers are visible. Line markers
// are consumed to recover user-visible positions.
[[nodiscard]] TokenStream lex(std::string_view text, std::string_view initial_file);

} // namespace cppl::frontend
