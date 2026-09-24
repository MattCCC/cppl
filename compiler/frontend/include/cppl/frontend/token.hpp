#pragma once

#include "cppl/source/location.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

// Where a line marker says a file was entered from: the file, by index, and the
// line of its `#include`.
struct IncludeSite {
    std::uint32_t file = 0;
    std::uint32_t line = 0;
};

// A lexed buffer together with the file names its line markers referred to.
class TokenStream {
  public:
    TokenStream(std::string_view text, std::vector<Token> tokens, std::vector<std::string> files,
                std::vector<bool> system_files = {}, std::vector<std::optional<IncludeSite>> include_sites = {},
                std::vector<source::ByteSpan> comments = {})
        : text_(text),
          tokens_(std::move(tokens)),
          files_(std::move(files)),
          system_files_(std::move(system_files)),
          include_sites_(std::move(include_sites)),
          comments_(std::move(comments)) {}

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
    // Every comment the lexer passed over, `//` to the end of its line or `/*`
    // through `*/`, in order. A preprocessor removes comments, so a compile's
    // stream has none; an editor's, lexed from the text as written, has each.
    [[nodiscard]] const std::vector<source::ByteSpan>& comments() const noexcept {
        return comments_;
    }

    // Whether a line marker entered the file as a system header.
    [[nodiscard]] bool is_system(std::uint32_t file) const noexcept {
        return file < system_files_.size() && system_files_[file];
    }

    // The `#include` that first entered `file`, as the file and line it is
    // written at, or nothing for a file no marker says was included.
    [[nodiscard]] std::optional<source::SourceLocation> included_at(std::string_view file) const;

    [[nodiscard]] source::SourceLocation location_of(const Token& token) const;

    // A file's text as its author wrote it, or nothing when it cannot be read.
    using WrittenText = std::function<std::optional<std::string>(const std::string& file)>;

    // Gives tokens the columns they were written at.
    //
    // A preprocessor keeps the column of a line's first token but writes every
    // later run of whitespace, and every comment, as one space, so a later
    // token's column in its output is not where the author wrote it. Where a
    // line's tokens spell what the written line spells, from its start or from
    // its end, they take the written columns; a macro's expansion matches
    // neither and keeps the column it has. System headers are left as they are.
    void use_written_columns(const WrittenText& written);

  private:
    std::string_view text_;
    std::vector<Token> tokens_;
    std::vector<std::string> files_;
    std::vector<bool> system_files_;
    std::vector<std::optional<IncludeSite>> include_sites_;
    std::vector<source::ByteSpan> comments_;
};

// Lexes preprocessed C++ text.
//
// The frontend runs after preprocessing (SPEC.md 3.2), so macros are already
// expanded and C++L constructs contributed by headers are visible. Line markers
// are consumed to recover user-visible positions.
[[nodiscard]] TokenStream lex(std::string_view text, std::string_view initial_file);

} // namespace cppl::frontend
