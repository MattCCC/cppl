#include "cppl/lsp/json.hpp"

#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace cppl::lsp::json {

namespace {

// Arrays and objects nest by recursion, so the depth bounds the stack a body
// can consume. No JSON-RPC message an editor sends nests anywhere near this.
constexpr std::size_t kMaxNesting = 128;

class Parser {
  public:
    explicit Parser(std::string_view text) : text_(text) {}

    Value parse_document() {
        skip_whitespace();
        Value value = parse_value();
        skip_whitespace();
        if (position_ != text_.size()) {
            throw Error("trailing characters after JSON document");
        }
        return value;
    }

  private:
    std::string_view text_;
    std::size_t position_ = 0;
    std::size_t nesting_ = 0;

    [[nodiscard]] char peek() const {
        if (position_ >= text_.size()) {
            throw Error("unexpected end of JSON input");
        }
        return text_[position_];
    }

    char advance() {
        const char c = peek();
        ++position_;
        return c;
    }

    void expect(char expected) {
        if (advance() != expected) {
            throw Error(std::string("expected '") + expected + "'");
        }
    }

    void skip_whitespace() {
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++position_;
            } else {
                break;
            }
        }
    }

    [[nodiscard]] bool starts_with(std::string_view literal) const {
        return text_.substr(position_, literal.size()) == literal;
    }

    Value parse_value() {
        skip_whitespace();
        const char c = peek();
        switch (c) {
            case '{':
            case '[': {
                // A failure abandons the parser, so the count is not restored
                // on that path.
                if (nesting_ == kMaxNesting) {
                    throw Error("JSON nested more than 128 levels deep");
                }
                ++nesting_;
                Value nested = c == '{' ? parse_object() : parse_array();
                --nesting_;
                return nested;
            }
            case '"':
                return {parse_string()};
            case 't':
                if (!starts_with("true")) {
                    throw Error("invalid literal");
                }
                position_ += 4;
                return {true};
            case 'f':
                if (!starts_with("false")) {
                    throw Error("invalid literal");
                }
                position_ += 5;
                return {false};
            case 'n':
                if (!starts_with("null")) {
                    throw Error("invalid literal");
                }
                position_ += 4;
                return {nullptr};
            default:
                return parse_number();
        }
    }

    Value parse_object() {
        expect('{');
        Object object;
        skip_whitespace();
        if (position_ < text_.size() && peek() == '}') {
            ++position_;
            return {std::move(object)};
        }
        while (true) {
            skip_whitespace();
            std::string key = parse_string();
            skip_whitespace();
            expect(':');
            Value value = parse_value();
            object.emplace_back(std::move(key), std::move(value));
            skip_whitespace();
            const char next = advance();
            if (next == ',') {
                continue;
            }
            if (next == '}') {
                break;
            }
            throw Error("expected ',' or '}' in object");
        }
        return {std::move(object)};
    }

    Value parse_array() {
        expect('[');
        Array array;
        skip_whitespace();
        if (position_ < text_.size() && peek() == ']') {
            ++position_;
            return {std::move(array)};
        }
        while (true) {
            array.push_back(parse_value());
            skip_whitespace();
            const char next = advance();
            if (next == ',') {
                continue;
            }
            if (next == ']') {
                break;
            }
            throw Error("expected ',' or ']' in array");
        }
        return {std::move(array)};
    }

    std::string parse_string() {
        expect('"');
        std::string result;
        while (true) {
            const char c = advance();
            if (c == '"') {
                break;
            }
            if (c == '\\') {
                const char escaped = advance();
                switch (escaped) {
                    case '"':
                        result.push_back('"');
                        break;
                    case '\\':
                        result.push_back('\\');
                        break;
                    case '/':
                        result.push_back('/');
                        break;
                    case 'b':
                        result.push_back('\b');
                        break;
                    case 'f':
                        result.push_back('\f');
                        break;
                    case 'n':
                        result.push_back('\n');
                        break;
                    case 'r':
                        result.push_back('\r');
                        break;
                    case 't':
                        result.push_back('\t');
                        break;
                    case 'u':
                        append_unicode_escape(result);
                        break;
                    default:
                        throw Error("invalid escape sequence");
                }
                continue;
            }
            result.push_back(c);
        }
        return result;
    }

    void append_unicode_escape(std::string& out) {
        const std::uint32_t codepoint = read_hex4();
        std::uint32_t scalar = codepoint;
        // A surrogate pair: LSP text (JSON strings) may contain characters
        // outside the BMP, encoded as two \u escapes.
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (!starts_with("\\u")) {
                throw Error("unpaired high surrogate in JSON string");
            }
            position_ += 2;
            const std::uint32_t low = read_hex4();
            if (low < 0xDC00 || low > 0xDFFF) {
                throw Error("invalid low surrogate in JSON string");
            }
            scalar = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
        }
        encode_utf8(scalar, out);
    }

    std::uint32_t read_hex4() {
        if (position_ + 4 > text_.size()) {
            throw Error("truncated unicode escape");
        }
        std::uint32_t value = 0;
        const auto [ptr, error] = std::from_chars(text_.data() + position_, text_.data() + position_ + 4, value, 16);
        if (error != std::errc{} || ptr != text_.data() + position_ + 4) {
            throw Error("invalid unicode escape");
        }
        position_ += 4;
        return value;
    }

    static void encode_utf8(std::uint32_t scalar, std::string& out) {
        if (scalar <= 0x7F) {
            out.push_back(static_cast<char>(scalar));
        } else if (scalar <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (scalar >> 6)));
            out.push_back(static_cast<char>(0x80 | (scalar & 0x3F)));
        } else if (scalar <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (scalar >> 12)));
            out.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (scalar & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (scalar >> 18)));
            out.push_back(static_cast<char>(0x80 | ((scalar >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (scalar & 0x3F)));
        }
    }

    Value parse_number() {
        const std::size_t start = position_;
        if (position_ < text_.size() && (text_[position_] == '-' || text_[position_] == '+')) {
            ++position_;
        }
        while (position_ < text_.size() &&
               (std::isdigit(static_cast<unsigned char>(text_[position_])) || text_[position_] == '.' ||
                text_[position_] == 'e' || text_[position_] == 'E' || text_[position_] == '+' ||
                text_[position_] == '-')) {
            ++position_;
        }
        if (position_ == start) {
            throw Error("invalid JSON value");
        }
        double value = 0.0;
        const std::string token(text_.substr(start, position_ - start));
        try {
            std::size_t consumed = 0;
            value = std::stod(token, &consumed);
            if (consumed != token.size()) {
                throw Error("invalid number");
            }
        } catch (const std::exception&) {
            throw Error("invalid number literal");
        }
        return {value};
    }
};

void dump_string(const std::string& text, std::string& out) {
    out.push_back('"');
    for (const char character : text) {
        const auto c = static_cast<unsigned char>(character);
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    constexpr std::string_view hex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[static_cast<std::size_t>(c >> 4)]);
                    out.push_back(hex[static_cast<std::size_t>(c & 0xF)]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

void dump_number(double value, std::string& out) {
    if (std::isfinite(value) && value == std::floor(value) && std::abs(value) < 1e15) {
        out += std::to_string(static_cast<long long>(value));
        return;
    }
    // The shortest text that reads back as the same double. A stream would keep
    // six significant digits, and a client could not match a response to a
    // request whose id had changed on the way back.
    std::array<char, 32> buffer{};
    const std::to_chars_result written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    out.append(buffer.data(), written.ptr);
}

void dump_value(const Value& value, std::string& out);

void dump_array(const Array& array, std::string& out) {
    out.push_back('[');
    for (std::size_t i = 0; i < array.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        dump_value(array[i], out);
    }
    out.push_back(']');
}

void dump_object(const Object& object, std::string& out) {
    out.push_back('{');
    for (std::size_t i = 0; i < object.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        dump_string(object[i].first, out);
        out.push_back(':');
        dump_value(object[i].second, out);
    }
    out.push_back('}');
}

void dump_value(const Value& value, std::string& out) {
    switch (value.type()) {
        case Type::Null:
            out += "null";
            break;
        case Type::Boolean:
            out += value.as_boolean() ? "true" : "false";
            break;
        case Type::Number:
            dump_number(value.as_number(), out);
            break;
        case Type::String:
            dump_string(value.as_string(), out);
            break;
        case Type::Array:
            dump_array(value.as_array(), out);
            break;
        case Type::Object:
            dump_object(value.as_object(), out);
            break;
    }
}

} // namespace

std::string Value::dump() const {
    std::string out;
    dump_value(*this, out);
    return out;
}

Value parse(std::string_view text) {
    Parser parser(text);
    return parser.parse_document();
}

} // namespace cppl::lsp::json
