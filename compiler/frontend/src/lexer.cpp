#include "cppl/frontend/token.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace cppl::frontend {

namespace {

// Longest match wins, so longer punctuators are listed first.
constexpr auto kPunctuators = std::to_array<std::string_view>(
    {"<<=", ">>=", "->*", "...", "<=>", "::", "->", "++", "--", "<<", ">>", ".*", "<=", ">=", "==", "!=", "&&",
     "||",  "+=",  "-=",  "*=",  "/=",  "%=", "^=", "&=", "|=", "##", "{",  "}",  "[",  "]",  "(",  ")",  ";",
     ":",   "?",   ".",   "+",   "-",   "*",  "/",  "%",  "^",  "&",  "|",  "~",  "!",  "=",  "<",  ">"});

bool is_identifier_start(unsigned char character) {
    return std::isalpha(character) != 0 || character == '_' || character >= 0x80;
}

bool is_identifier_continue(unsigned char character) {
    return std::isalnum(character) != 0 || character == '_' || character >= 0x80;
}

bool is_horizontal_space(char character) {
    return character == ' ' || character == '\t' || character == '\r' || character == '\f' || character == '\v';
}

struct LiteralPrefix {
    std::size_t length = 0;
    bool raw = false;
    bool character = false;
    bool valid = false;
};

// Recognizes a string or character literal, including its encoding prefix, so
// that text inside literals is never mistaken for code.
LiteralPrefix match_literal_prefix(std::string_view text, std::size_t offset) {
    static constexpr std::array<std::string_view, 5> kEncodings = {"u8", "u", "U", "L", ""};

    for (std::string_view encoding : kEncodings) {
        if (text.compare(offset, encoding.size(), encoding) != 0) {
            continue;
        }
        std::size_t cursor = offset + encoding.size();
        bool raw = false;
        if (cursor < text.size() && text[cursor] == 'R') {
            raw = true;
            ++cursor;
        }
        if (cursor >= text.size()) {
            continue;
        }
        if (text[cursor] == '"') {
            return LiteralPrefix{cursor + 1 - offset, raw, false, true};
        }
        if (text[cursor] == '\'' && !raw) {
            return LiteralPrefix{cursor + 1 - offset, false, true, true};
        }
    }
    return LiteralPrefix{};
}

class Lexer {
  public:
    Lexer(std::string_view text, std::string_view initial_file) : text_(text) {
        files_.emplace_back(initial_file);
    }

    TokenStream run() {
        while (offset_ < text_.size()) {
            if (at_line_start_ && consume_line_marker()) {
                continue;
            }
            const char character = text_[offset_];

            if (character == '\n') {
                advance_line();
                continue;
            }
            if (is_horizontal_space(character)) {
                ++offset_;
                continue;
            }
            if (consume_comment()) {
                continue;
            }

            at_line_start_ = false;
            lex_token();
        }

        Token end;
        end.kind = TokenKind::EndOfFile;
        end.span = source::ByteSpan{offset_, 0};
        end.file = file_index_;
        end.line = presumed_line_;
        end.column = column();
        tokens_.push_back(end);

        return {text_, std::move(tokens_), std::move(files_)};
    }

  private:
    [[nodiscard]] std::uint32_t column() const {
        return static_cast<std::uint32_t>(offset_ - line_start_ + 1);
    }

    void advance_line() {
        ++offset_;
        line_start_ = offset_;
        ++presumed_line_;
        at_line_start_ = true;
    }

    bool consume_comment() {
        if (offset_ + 1 >= text_.size() || text_[offset_] != '/') {
            return false;
        }
        if (text_[offset_ + 1] == '/') {
            while (offset_ < text_.size() && text_[offset_] != '\n') {
                ++offset_;
            }
            return true;
        }
        if (text_[offset_ + 1] == '*') {
            offset_ += 2;
            while (offset_ < text_.size()) {
                if (text_[offset_] == '\n') {
                    advance_line();
                    continue;
                }
                if (text_[offset_] == '*' && offset_ + 1 < text_.size() && text_[offset_ + 1] == '/') {
                    offset_ += 2;
                    return true;
                }
                ++offset_;
            }
            return true;
        }
        return false;
    }

    // Consumes a preprocessor line marker and adopts the position it states.
    // Any other directive line (a surviving #pragma) is skipped whole.
    bool consume_line_marker() {
        std::size_t cursor = offset_;
        while (cursor < text_.size() && is_horizontal_space(text_[cursor])) {
            ++cursor;
        }
        if (cursor >= text_.size() || text_[cursor] != '#') {
            return false;
        }

        ++cursor;
        while (cursor < text_.size() && is_horizontal_space(text_[cursor])) {
            ++cursor;
        }
        if (text_.compare(cursor, 4, "line") == 0) {
            cursor += 4;
            while (cursor < text_.size() && is_horizontal_space(text_[cursor])) {
                ++cursor;
            }
        }

        std::uint32_t stated_line = 0;
        bool has_line = false;
        while (cursor < text_.size() && std::isdigit(static_cast<unsigned char>(text_[cursor]))) {
            stated_line = stated_line * 10 + static_cast<std::uint32_t>(text_[cursor] - '0');
            has_line = true;
            ++cursor;
        }

        std::string stated_file;
        bool has_file = false;
        while (cursor < text_.size() && is_horizontal_space(text_[cursor])) {
            ++cursor;
        }
        if (cursor < text_.size() && text_[cursor] == '"') {
            ++cursor;
            has_file = true;
            while (cursor < text_.size() && text_[cursor] != '"' && text_[cursor] != '\n') {
                if (text_[cursor] == '\\' && cursor + 1 < text_.size()) {
                    ++cursor;
                }
                stated_file.push_back(text_[cursor]);
                ++cursor;
            }
            if (cursor < text_.size() && text_[cursor] == '"') {
                ++cursor;
            }
        }

        // Skip the remainder of the directive line.
        while (cursor < text_.size() && text_[cursor] != '\n') {
            ++cursor;
        }

        // A directive carrying a line number is a position marker; anything
        // else that survived preprocessing (a #pragma) only costs a line.
        const bool is_marker = has_line;
        offset_ = cursor;
        if (offset_ < text_.size()) {
            ++offset_; // the newline
        }
        line_start_ = offset_;
        at_line_start_ = true;

        if (is_marker) {
            presumed_line_ = stated_line;
            if (has_file) {
                file_index_ = intern(stated_file);
            }
        } else {
            ++presumed_line_;
        }
        return true;
    }

    std::uint32_t intern(const std::string& file) {
        for (std::size_t index = 0; index < files_.size(); ++index) {
            if (files_[index] == file) {
                return static_cast<std::uint32_t>(index);
            }
        }
        files_.push_back(file);
        return static_cast<std::uint32_t>(files_.size() - 1);
    }

    void push(TokenKind kind, std::size_t start, std::uint32_t start_column) {
        Token token;
        token.kind = kind;
        token.span = source::ByteSpan{start, offset_ - start};
        token.text = text_.substr(start, offset_ - start);
        token.file = file_index_;
        token.line = presumed_line_;
        token.column = start_column;
        tokens_.push_back(token);
    }

    void lex_token() {
        const std::size_t start = offset_;
        const std::uint32_t start_column = column();

        const LiteralPrefix literal = match_literal_prefix(text_, offset_);
        if (literal.valid) {
            consume_literal(literal);
            push(literal.character ? TokenKind::CharLiteral : TokenKind::StringLiteral, start, start_column);
            return;
        }

        const auto character = static_cast<unsigned char>(text_[offset_]);
        if (is_identifier_start(character)) {
            while (offset_ < text_.size() && is_identifier_continue(static_cast<unsigned char>(text_[offset_]))) {
                ++offset_;
            }
            push(TokenKind::Identifier, start, start_column);
            return;
        }

        if (std::isdigit(character) != 0 || (character == '.' && offset_ + 1 < text_.size() &&
                                             std::isdigit(static_cast<unsigned char>(text_[offset_ + 1])) != 0)) {
            consume_number();
            push(TokenKind::Number, start, start_column);
            return;
        }

        for (std::string_view punctuator : kPunctuators) {
            if (text_.compare(offset_, punctuator.size(), punctuator) == 0) {
                offset_ += punctuator.size();
                push(TokenKind::Punctuator, start, start_column);
                return;
            }
        }

        ++offset_;
        push(TokenKind::Punctuator, start, start_column);
    }

    void consume_number() {
        while (offset_ < text_.size()) {
            const char character = text_[offset_];
            if (std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_' || character == '.' ||
                character == '\'') {
                const bool exponent = character == 'e' || character == 'E' || character == 'p' || character == 'P';
                ++offset_;
                if (exponent && offset_ < text_.size() && (text_[offset_] == '+' || text_[offset_] == '-')) {
                    ++offset_;
                }
                continue;
            }
            break;
        }
    }

    void consume_literal(const LiteralPrefix& literal) {
        offset_ += literal.length;

        if (literal.raw) {
            std::string delimiter;
            while (offset_ < text_.size() && text_[offset_] != '(') {
                delimiter.push_back(text_[offset_]);
                ++offset_;
            }
            if (offset_ < text_.size()) {
                ++offset_; // '('
            }
            const std::string terminator = ")" + delimiter + "\"";
            while (offset_ < text_.size()) {
                if (text_.compare(offset_, terminator.size(), terminator) == 0) {
                    offset_ += terminator.size();
                    return;
                }
                if (text_[offset_] == '\n') {
                    advance_line();
                    continue;
                }
                ++offset_;
            }
            return;
        }

        const char terminator = literal.character ? '\'' : '"';
        while (offset_ < text_.size()) {
            const char character = text_[offset_];
            if (character == '\\' && offset_ + 1 < text_.size()) {
                offset_ += 2;
                continue;
            }
            if (character == terminator) {
                ++offset_;
                return;
            }
            if (character == '\n') {
                return; // an unterminated literal; Clang reports it
            }
            ++offset_;
        }
    }

    std::string_view text_;
    std::vector<Token> tokens_;
    std::vector<std::string> files_;
    std::size_t offset_ = 0;
    std::size_t line_start_ = 0;
    std::uint32_t presumed_line_ = 1;
    std::uint32_t file_index_ = 0;
    bool at_line_start_ = true;
};

} // namespace

source::SourceLocation TokenStream::location_of(const Token& token) const {
    source::SourceLocation location;
    if (token.file < files_.size()) {
        location.file = files_[token.file];
    }
    location.line = token.line;
    location.column = token.column;
    return location;
}

TokenStream lex(std::string_view text, std::string_view initial_file) {
    return Lexer(text, initial_file).run();
}

} // namespace cppl::frontend
