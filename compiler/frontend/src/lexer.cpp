#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
        system_files_.push_back(false);
        include_sites_.emplace_back();
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

        return {text_, std::move(tokens_), std::move(files_), std::move(system_files_), std::move(include_sites_)};
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

        // The flags after the file name: 1 says the file is being entered from
        // the current line, and 3 that it is a system header.
        bool entering = false;
        bool system = false;
        while (cursor < text_.size() && text_[cursor] != '\n') {
            if (std::isdigit(static_cast<unsigned char>(text_[cursor])) == 0) {
                ++cursor;
                continue;
            }
            std::uint32_t flag = 0;
            while (cursor < text_.size() && std::isdigit(static_cast<unsigned char>(text_[cursor])) != 0) {
                flag = flag * 10 + static_cast<std::uint32_t>(text_[cursor] - '0');
                ++cursor;
            }
            entering = entering || flag == 1;
            system = system || flag == 3;
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
            if (has_file) {
                const std::uint32_t entered = intern(stated_file);
                // Entered from the line this marker stands on, which is the
                // line of the `#include` it replaces.
                if (entering && !include_sites_[entered].has_value()) {
                    include_sites_[entered] = IncludeSite{file_index_, presumed_line_};
                }
                file_index_ = entered;
                if (system) {
                    system_files_[file_index_] = true;
                }
            }
            presumed_line_ = stated_line;
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
        system_files_.push_back(false);
        include_sites_.emplace_back();
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
    std::vector<bool> system_files_;
    std::vector<std::optional<IncludeSite>> include_sites_;
    std::size_t offset_ = 0;
    std::size_t line_start_ = 0;
    std::uint32_t presumed_line_ = 1;
    std::uint32_t file_index_ = 0;
    bool at_line_start_ = true;
};

struct WrittenToken {
    std::string text;
    std::uint32_t column = 0;
};

// The tokens of a written file, by the line each was written on.
using WrittenLines = std::map<std::uint32_t, std::vector<WrittenToken>>;

WrittenLines written_lines(std::string_view text, const std::string& file) {
    WrittenLines lines;
    for (const Token& token : lex(text, file).tokens()) {
        // A `#line` naming another file moves what follows out of this one.
        if (token.kind != TokenKind::EndOfFile && token.file == 0) {
            lines[token.line].push_back({std::string(token.text), token.column});
        }
    }
    return lines;
}

// Where tokens[begin, end), one line of preprocessed output, spell what `words`,
// the same line as written, spells, from the start or from the end, they take
// the written columns.
void adopt_written_columns(std::vector<Token>& tokens, std::size_t begin, std::size_t end,
                           const std::vector<WrittenToken>& words) {
    const std::size_t count = end - begin;
    std::size_t front = 0;
    while (front < count && front < words.size() && tokens[begin + front].text == words[front].text) {
        tokens[begin + front].column = words[front].column;
        ++front;
    }
    std::size_t back = 0;
    while (front + back < count && front + back < words.size() &&
           tokens[end - 1 - back].text == words[words.size() - 1 - back].text) {
        tokens[end - 1 - back].column = words[words.size() - 1 - back].column;
        ++back;
    }
}

} // namespace

std::optional<source::SourceLocation> TokenStream::included_at(std::string_view file) const {
    for (std::size_t index = 0; index < files_.size() && index < include_sites_.size(); ++index) {
        if (files_[index] != file) {
            continue;
        }
        if (const std::optional<IncludeSite>& site = include_sites_[index]; site.has_value()) {
            return source::SourceLocation{files_[site->file], site->line, 1};
        }
    }
    return std::nullopt;
}

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

void TokenStream::use_written_columns(const WrittenText& written) {
    // Each file is read and lexed once, the first time a line of it is met.
    std::vector<std::optional<WrittenLines>> by_file(files_.size());
    std::vector<bool> read(files_.size(), false);
    const auto lines_of = [&](std::uint32_t file) -> const WrittenLines* {
        if (!read[file]) {
            read[file] = true;
            if (const std::optional<std::string> text = written(files_[file])) {
                by_file[file] = written_lines(*text, files_[file]);
            }
        }
        return by_file[file] ? &*by_file[file] : nullptr;
    };

    std::size_t begin = 0;
    while (begin < tokens_.size()) {
        const std::uint32_t file = tokens_[begin].file;
        const std::uint32_t line = tokens_[begin].line;
        std::size_t end = begin;
        while (end < tokens_.size() && tokens_[end].kind != TokenKind::EndOfFile && tokens_[end].file == file &&
               tokens_[end].line == line) {
            ++end;
        }
        if (end == begin) {
            ++begin;
            continue;
        }
        if (file < files_.size() && !is_system(file)) {
            if (const WrittenLines* lines = lines_of(file)) {
                if (const auto found = lines->find(line); found != lines->end()) {
                    adopt_written_columns(tokens_, begin, end, found->second);
                }
            }
        }
        begin = end;
    }
}

} // namespace cppl::frontend
