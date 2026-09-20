#include "formal_projection.hpp"

#include <algorithm>
#include <utility>

namespace cppl::frontend::detail {

std::string quote_path(std::string_view path) {
    std::string quoted = "\"";
    for (char character : path) {
        if (character == '\\' || character == '"') {
            quoted.push_back('\\');
        }
        quoted.push_back(character);
    }
    quoted.push_back('"');
    return quoted;
}

// Inserted text changes physical line numbering, so each insertion states the
// line it stands for and restores the numbering after itself. Diagnostics from
// inside a specification function then point at the law that produced it.
std::string line_directive(std::uint32_t line, std::string_view file) {
    if (file.empty() || line == 0) {
        return {};
    }
    return "#line " + std::to_string(line) + " " + quote_path(file) + "\n";
}

struct EqualitySyntax {
    source::ByteSpan type;
    source::ByteSpan arguments;
    source::SourceLocation arguments_location;
};

// Delimit only the formal wrapper. In particular the arguments are copied as
// one C++ argument list: templates, commas, lookup and conversions belong to
// Clang. Ordinary expressions that are not this complete form are untouched.
std::optional<EqualitySyntax> equality_syntax(const TokenStream& stream, source::ByteSpan expression) {
    const auto& tokens = stream.tokens();
    std::size_t begin = static_cast<std::size_t>(
        std::lower_bound(tokens.begin(), tokens.end(), expression.offset,
                         [](const Token& token, std::size_t offset) { return token.span.offset < offset; }) -
        tokens.begin());
    std::size_t end = begin;
    while (end < tokens.size() && tokens[end].span.end() <= expression.end() &&
           tokens[end].kind != TokenKind::EndOfFile)
        ++end;
    const auto matching = [&](std::size_t from, std::string_view open, std::string_view close) {
        unsigned depth = 0;
        for (std::size_t i = from; i < end; ++i) {
            if (tokens[i].text == open)
                ++depth;
            if (tokens[i].text == close && --depth == 0)
                return i;
        }
        return end;
    };
    while (begin < end && tokens[begin].text == "(" && matching(begin, "(", ")") == end - 1) {
        ++begin;
        --end;
    }
    if (end - begin < 6 || tokens[begin].text != "Eq" || tokens[begin + 1].text != "<")
        return std::nullopt;
    unsigned angles = 1;
    std::size_t close = begin + 2;
    for (; close < end; ++close) {
        const auto token = tokens[close].text;
        if (token == "(" || token == "[") {
            close = matching(close, token, token == "(" ? ")" : "]");
            if (close == end)
                return std::nullopt;
        } else if (token == "<") {
            ++angles;
        } else if (token == ">" || token == ">>") {
            const unsigned count = token == ">>" ? 2 : 1;
            if (count > angles)
                return std::nullopt;
            if (angles <= count)
                break;
            angles -= count;
        }
    }
    if (close + 1 >= end || tokens[close + 1].text != "(" || matching(close + 1, "(", ")") != end - 1)
        return std::nullopt;
    // The final '>' can be the second character of a C++ '>>' token.
    const std::size_t type_end = tokens[close].span.offset + (tokens[close].text == ">>" && angles == 2 ? 1 : 0);
    auto location = stream.location_of(tokens[close + 1]);
    ++location.column;
    return EqualitySyntax{{tokens[begin + 2].span.offset, type_end - tokens[begin + 2].span.offset},
                          {tokens[close + 1].span.end(), tokens[end - 1].span.offset - tokens[close + 1].span.end()},
                          std::move(location)};
}

bool contains_formal_equality(const TokenStream& stream, source::ByteSpan expression) {
    const auto& tokens = stream.tokens();
    const auto start =
        std::lower_bound(tokens.begin(), tokens.end(), expression.offset,
                         [](const Token& token, std::size_t offset) { return token.span.offset < offset; });
    for (std::size_t index = static_cast<std::size_t>(start - tokens.begin());
         index + 1 < tokens.size() && tokens[index].span.offset < expression.end(); ++index) {
        if (tokens[index + 1].span.end() <= expression.end() && tokens[index].text == "Eq" &&
            tokens[index + 1].text == "<")
            return true;
    }
    return false;
}

namespace {

using Kind = source::ProjectionKind;

std::pair<std::size_t, std::size_t> token_range(const TokenStream& stream, source::ByteSpan expression) {
    const auto& tokens = stream.tokens();
    auto first = std::lower_bound(tokens.begin(), tokens.end(), expression.offset,
                                  [](const Token& token, std::size_t offset) { return token.span.offset < offset; });
    auto last = first;
    while (last != tokens.end() && last->kind != TokenKind::EndOfFile && last->span.end() <= expression.end())
        ++last;
    return {static_cast<std::size_t>(first - tokens.begin()), static_cast<std::size_t>(last - tokens.begin())};
}

std::size_t matching(const std::vector<Token>& tokens, std::size_t begin, std::size_t end) {
    const auto open = tokens[begin].text;
    const auto close = open == "(" ? ")" : open == "{" ? "}" : "]";
    unsigned depth = 0;
    for (std::size_t index = begin; index < end; ++index) {
        if (tokens[index].text == open)
            ++depth;
        if (tokens[index].text == close && --depth == 0)
            return index;
    }
    return end;
}

// A quantifier word is formal only in the complete form the grammar states:
// `forall (parameters) { proposition }` (GRAMMAR.md 28). Anything else spelled
// `forall` or `exists` is an ordinary C++ identifier and stays Clang's, so a
// program that already uses those names keeps its own meaning (SPEC.md 3.1).
// Returns the index one past the closing brace, or `begin` for any other
// tokens, which a complete form can never occupy.
std::size_t quantifier_form(const std::vector<Token>& tokens, std::size_t begin, std::size_t end) {
    if (begin >= end || !(tokens[begin].is_identifier("forall") || tokens[begin].is_identifier("exists")))
        return begin;
    if (begin + 1 >= end || tokens[begin + 1].text != "(")
        return begin;
    const std::size_t parameters = matching(tokens, begin + 1, end);
    if (parameters + 1 >= end || tokens[parameters + 1].text != "{")
        return begin;
    const std::size_t body = matching(tokens, parameters + 1, end);
    return body == end ? begin : body + 1;
}

source::ByteSpan span_of(const std::vector<Token>& tokens, std::size_t begin, std::size_t end) {
    if (begin == end)
        return {tokens[begin].span.offset, 0};
    return {tokens[begin].span.offset, tokens[end - 1].span.end() - tokens[begin].span.offset};
}

std::string copied(const TokenStream& stream, source::ByteSpan expression) {
    const auto [begin, end] = token_range(stream, expression);
    if (begin == end)
        return {};
    const auto location = stream.location_of(stream.tokens()[begin]);
    return "\n" + line_directive(location.line, location.file) + std::string(location.column - 1, ' ') +
           std::string(stream.spelling(span_of(stream.tokens(), begin, end))) + "\n";
}

// Parentheses that wrap the whole proposition say nothing about its form.
void strip_parentheses(const std::vector<Token>& tokens, std::size_t& begin, std::size_t& end) {
    while (begin < end && tokens[begin].text == "(" && matching(tokens, begin, end) == end - 1) {
        ++begin;
        --end;
    }
}

// Implication is looser than every ordinary C++ operator (GRAMMAR.md 33), so
// only an `->` outside all brackets is one. Parenthesized C++ expressions,
// argument lists and quantifier bodies stay opaque here; Clang alone parses
// the C++ leaves, and a member access written inside them keeps its C++
// meaning. Returns `end` when the proposition states no implication, or
// `unbalanced` when the delimiters do not nest.
constexpr std::size_t kUnbalanced = static_cast<std::size_t>(-1);

// `<->` is logical equivalence (GRAMMAR.md 30). C++ has no such punctuator, so
// it reaches here as a `<` the `->` follows with nothing in between.
bool is_equivalence(const std::vector<Token>& tokens, std::size_t begin, std::size_t index) {
    return index > begin && tokens[index - 1].text == "<" && tokens[index - 1].span.end() == tokens[index].span.offset;
}

std::size_t implication_operator(const std::vector<Token>& tokens, std::size_t begin, std::size_t end) {
    for (std::size_t index = begin; index < end; ++index) {
        const auto token = tokens[index].text;
        if (token == "(" || token == "{" || token == "[") {
            index = matching(tokens, index, end);
            if (index == end)
                return kUnbalanced;
        } else if (token == "->") {
            return index;
        }
    }
    return end;
}

// Equivalence associates to the left; implication associates to the right.
// Bracketed C++ expressions remain opaque and are always resolved by Clang.
std::size_t connective_operator(const std::vector<Token>& tokens, std::size_t begin, std::size_t end, Kind kind) {
    std::size_t found = end;
    for (std::size_t index = begin; index < end; ++index) {
        const auto token = tokens[index].text;
        if (token == "(" || token == "{" || token == "[") {
            index = matching(tokens, index, end);
            if (index == end)
                return kUnbalanced;
        } else if ((kind == Kind::Equivalence && token == "->" && is_equivalence(tokens, begin, index)) ||
                   (kind == Kind::Disjunction && token == "||") || (kind == Kind::Conjunction && token == "&&")) {
            found = index;
        }
    }
    return found;
}

FormulaProjection formula(const TokenStream& stream, source::ByteSpan expression, unsigned depth) {
    if (depth > 128)
        return {{}, {}, "proposition nesting exceeds the supported limit"};
    const auto& tokens = stream.tokens();
    auto [begin, end] = token_range(stream, expression);
    strip_parentheses(tokens, begin, end);
    if (begin == end)
        return {{}, {}, "a proposition cannot be empty"};
    expression = span_of(tokens, begin, end);

    const auto paired = [&](Kind kind, std::size_t before, std::size_t after) {
        auto left = formula(stream, span_of(tokens, begin, before), depth + 1);
        auto right = formula(stream, span_of(tokens, after, end), depth + 1);
        if (left.failure)
            return left;
        if (right.failure)
            return right;
        return FormulaProjection{
            {kind, {left.shape, right.shape}}, "[=]() { (" + left.expression + "); (" + right.expression + "); }", {}};
    };

    const std::size_t equivalence = connective_operator(tokens, begin, end, Kind::Equivalence);
    if (equivalence == kUnbalanced)
        return {{}, {}, "unbalanced proposition delimiters"};
    if (equivalence != end)
        return paired(Kind::Equivalence, equivalence - 1, equivalence + 1);

    // Implication is right associative, so the first operator carries the rest.
    const std::size_t arrow = implication_operator(tokens, begin, end);
    if (arrow == kUnbalanced)
        return {{}, {}, "unbalanced proposition delimiters"};
    if (arrow != end)
        return paired(Kind::Implication, arrow, arrow + 1);

    if (quantifier_form(tokens, begin, end) == end) {
        if (tokens[begin].is_identifier("exists"))
            return {{}, {}, "existential quantification is not supported yet"};
        const auto close = matching(tokens, begin + 1, end);
        if (close == begin + 2)
            return {{}, {}, "forall requires at least one binder"};
        auto body = formula(stream, span_of(tokens, close + 2, end - 1), depth + 1);
        if (body.failure)
            return body;
        return {{Kind::Universal, {body.shape}},
                "[=](" + copied(stream, span_of(tokens, begin + 2, close)) + ") { return (" + body.expression + "); }",
                {}};
    }

    if (const auto eq = equality_syntax(stream, expression); eq && !contains_formal_equality(stream, eq->arguments)) {
        const auto type = std::string(stream.spelling(eq->type));
        return {{Kind::Equality, {}}, "([](" + type + ", " + type + ") {})(" + copied(stream, eq->arguments) + ")", {}};
    }
    if (contains_formal_syntax(stream, expression)) {
        // `||` is looser than `&&` (GRAMMAR.md 33), so when both stand at this
        // level the disjunction is the outer one.
        for (const Kind kind : {Kind::Disjunction, Kind::Conjunction}) {
            const std::size_t at = connective_operator(tokens, begin, end, kind);
            if (at != end && at != kUnbalanced)
                return paired(kind, at, at + 1);
        }
        return {{}, {}, "nested or malformed formal syntax is not supported in this proposition"};
    }
    return {{Kind::Expression, {}}, copied(stream, expression), {}};
}

} // namespace

// Whether the expression states anything C++ alone cannot: a complete
// quantifier form, the formal equality form, or an implication at the
// proposition's own level. Everything else is C++ and is left to Clang, so a
// program that spells an ordinary function `forall` or dereferences through
// `->` inside an expression keeps its own meaning (SPEC.md 3.1).
bool contains_formal_syntax(const TokenStream& stream, source::ByteSpan expression) {
    const auto& tokens = stream.tokens();
    auto [begin, end] = token_range(stream, expression);
    strip_parentheses(tokens, begin, end);
    if (implication_operator(tokens, begin, end) < end)
        return true;
    for (std::size_t index = begin; index < end; ++index) {
        if (quantifier_form(tokens, index, end) != index)
            return true;
        if (tokens[index].is_identifier("Eq") && index + 1 < end && tokens[index + 1].text == "<")
            return true;
    }
    return false;
}

FormulaProjection project_formula(const TokenStream& stream, source::ByteSpan expression) {
    return formula(stream, expression, 0);
}

} // namespace cppl::frontend::detail
