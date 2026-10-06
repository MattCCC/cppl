// Reading declarators: qualified names, template arguments, return types,
// parameters, and what a specifier introduces.

#include "cppl/frontend/token.hpp"
#include "recognizer_state.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::frontend {

using detail::recognizer::is_one_of;
using detail::recognizer::is_type_keyword;
using detail::recognizer::kSpecifiersAfterType;

namespace detail::recognizer {

// Whether the word at `index` is followed by `::`. Such a word is the first
// component of a C++ nested-name-specifier, `pure::inner`, whatever C++L gives
// the word elsewhere: a namespace or a class of that name is all it can denote
// there (SPEC.md 3.1, WORD-008).
bool names_a_scope(const std::vector<Token>& tokens, std::size_t index) {
    return index + 1 < tokens.size() && tokens[index + 1].is_punctuator("::");
}

// Whether the token at `index` is the C++L specifier `word`, rather than the
// first component of a qualified name spelled with it.
bool is_specifier(const std::vector<Token>& tokens, std::size_t index, std::string_view word) {
    return index < tokens.size() && tokens[index].is_identifier(word) && !names_a_scope(tokens, index);
}

} // namespace detail::recognizer

namespace {

// The token just past the template-argument list opened at `open`. The list
// closes at the `>` that balances its `<`, or at a `>>` closing two lists at
// once; a `>` inside parentheses or brackets is a comparison. Nothing when no
// such `>` comes before what cannot stand in an argument list.
std::optional<std::size_t> past_template_arguments(const std::vector<Token>& tokens, std::size_t open) {
    std::size_t angles = 0;
    std::size_t brackets = 0;
    for (std::size_t cursor = open; cursor < tokens.size(); ++cursor) {
        const Token& token = tokens[cursor];
        if (token.kind == TokenKind::EndOfFile || token.is_punctuator(";") || token.is_punctuator("{") ||
            token.is_punctuator("}")) {
            return std::nullopt;
        }
        if (token.is_punctuator("(") || token.is_punctuator("[")) {
            ++brackets;
        } else if (token.is_punctuator(")") || token.is_punctuator("]")) {
            if (brackets == 0) {
                return std::nullopt;
            }
            --brackets;
        } else if (brackets > 0) {
            continue;
        } else if (token.is_punctuator("<")) {
            ++angles;
        } else if (token.is_punctuator(">")) {
            if (--angles == 0) {
                return cursor + 1;
            }
        } else if (token.is_punctuator(">>")) {
            if (angles < 2) {
                return std::nullopt;
            }
            angles -= 2;
            if (angles == 0) {
                return cursor + 1;
            }
        }
    }
    return std::nullopt;
}

// The alternative spellings of operators (`and` for `&&`). They lex as
// identifiers, but `verified and ready;` is an expression, not a declaration.
constexpr auto kAlternativeOperators = std::to_array<std::string_view>(
    {"and", "and_eq", "bitand", "bitor", "compl", "not", "not_eq", "or", "or_eq", "xor", "xor_eq"});

} // namespace

namespace detail::recognizer {

bool is_one_of(const Token& token, std::span<const std::string_view> words) {
    return token.kind == TokenKind::Identifier && std::ranges::find(words, token.text) != words.end();
}

} // namespace detail::recognizer

namespace {

// The token just past the possibly qualified name beginning at `index`: `T`,
// `ns::T`, `::ns::T`, `C<int>::T` or `C<T>::template X<int>`. Nothing when the
// tokens there are not such a name. An operator-function-id, `operator*` or
// `C::operator&`, names a function and never a type, so it is not one either.
std::optional<std::size_t> past_qualified_name(const std::vector<Token>& tokens, std::size_t index) {
    std::size_t cursor = index;
    if (cursor < tokens.size() && tokens[cursor].is_punctuator("::")) {
        ++cursor;
    }
    while (true) {
        if (cursor < tokens.size() && tokens[cursor].is_identifier("template")) {
            ++cursor;
        }
        if (cursor >= tokens.size() || tokens[cursor].kind != TokenKind::Identifier ||
            tokens[cursor].is_identifier("operator") || is_one_of(tokens[cursor], kAlternativeOperators)) {
            return std::nullopt;
        }
        ++cursor;
        if (cursor < tokens.size() && tokens[cursor].is_punctuator("<")) {
            const std::optional<std::size_t> past = past_template_arguments(tokens, cursor);
            if (!past.has_value()) {
                return std::nullopt;
            }
            cursor = *past;
        }
        if (cursor >= tokens.size() || !tokens[cursor].is_punctuator("::")) {
            return cursor;
        }
        ++cursor;
    }
}

// Whether the tokens from `cursor` state a return type and then go on into a
// declarator. A name followed by an identifier or a pointer or reference
// operator is a type: were it the declarator's own name, as it is in `T x;`,
// `T(x)` or `ns::f()`, nothing but `(`, `[`, `=`, `{`, `,` or `;` could follow it.
//
// The specifiers C++ admits after a type name are passed over first, because
// they say nothing yet: `verified const int f()` returns `const int`, while
// `verified const c{};` declares `c` of type `verified const`. What follows
// them decides, as it does with none. `explicit` alone decides at once: it
// stands only on a constructor or a conversion function, which has no return
// type, so the word before it cannot be one.
bool begins_return_type(const std::vector<Token>& tokens, std::size_t cursor) {
    while (cursor < tokens.size() &&
           (is_one_of(tokens[cursor], kSpecifiersAfterType) || tokens[cursor].is_identifier("explicit"))) {
        if (tokens[cursor].is_identifier("explicit")) {
            return true;
        }
        ++cursor;
    }
    if (cursor >= tokens.size()) {
        return false;
    }
    if (is_type_keyword(tokens[cursor])) {
        return true;
    }
    const std::optional<std::size_t> past = past_qualified_name(tokens, cursor);
    if (!past.has_value() || *past >= tokens.size()) {
        return false;
    }
    const Token& after = tokens[*past];
    return after.kind == TokenKind::Identifier || after.is_punctuator("*") || after.is_punctuator("&") ||
           after.is_punctuator("&&");
}

} // namespace

namespace detail::recognizer {

// `pure`, `verified` and `unsafe` are declaration specifiers only where the
// following tokens cannot begin an ordinary declaration whose type carries that
// name: where a return type follows the word, and a declarator follows that
// (GRAMMAR.md 7). `verified pure` is both specifiers where a return type
// follows the second.
//
// A word followed by `::` is never a specifier. `pure::inner g();` declares a
// function returning a type of namespace `pure`, and reading the word as a
// specifier there would give the function a different return type, `::inner`,
// with no diagnostic at all.
bool specifier_introduces_declaration(const std::vector<Token>& tokens, std::size_t index) {
    const std::size_t next = index + 1;
    if (next >= tokens.size() || names_a_scope(tokens, index)) {
        return false;
    }
    if (!tokens[index].is_identifier("pure") && is_specifier(tokens, next, "pure") &&
        begins_return_type(tokens, next + 1)) {
        return true;
    }
    return begins_return_type(tokens, next);
}

} // namespace detail::recognizer

namespace {

// The start of the template-argument list ending at `index`, which must hold a
// `>`. An explicit specialization names its arguments in the declarator,
// `pick<4u>(unsigned)`, so the declarator's name is not the token before `(`
// (SPEC.md TEMPLATE-001).
//
// This is the same balanced scan `template_header_start` performs, in the same
// direction, and it is equally textual: which specialization the name denotes
// is Clang's to resolve, never this scan's.
std::optional<std::size_t> template_arguments_start(const std::vector<Token>& tokens, std::size_t index) {
    if (!tokens[index].is_punctuator(">")) {
        return std::nullopt;
    }
    std::size_t depth = 0;
    for (std::size_t scan = index;; --scan) {
        if (tokens[scan].is_punctuator(">")) {
            ++depth;
        } else if (tokens[scan].is_punctuator("<")) {
            --depth;
            if (depth == 0) {
                return scan;
            }
        } else if (tokens[scan].is_punctuator(";") || tokens[scan].is_punctuator("{") ||
                   tokens[scan].is_punctuator(")")) {
            return std::nullopt; // not an argument list: a comparison or worse
        }
        if (scan == 0) {
            return std::nullopt;
        }
    }
}

// C++ keywords a `(` may follow that never name what a declarator declares:
// `for (...) decreases (m) {i};` after a `;` is a loop, not a function `for`.
constexpr auto kNotDeclaratorNames = std::to_array<std::string_view>(
    {"alignas", "alignof", "catch", "co_await", "co_return", "co_yield", "delete", "for", "if", "new", "noexcept",
     "requires", "return", "sizeof", "static_assert", "switch", "throw", "typeid", "while"});

} // namespace

namespace detail::recognizer {

// Where the parameter list of the function declarator named at `name` opens: the
// token after the name, or, for an operator function, the token after the whole
// operator-function-id. `operator()` and `operator[]` hold brackets of their own,
// which are part of the name and never the parameter list; a conversion
// function's name runs to the first `(`.
std::size_t declarator_parameters(const std::vector<Token>& tokens, std::size_t name) {
    if (!tokens[name].is_identifier("operator")) {
        return name + 1;
    }
    const std::size_t at = name + 1;
    if (at + 1 < tokens.size() && ((tokens[at].is_punctuator("(") && tokens[at + 1].is_punctuator(")")) ||
                                   (tokens[at].is_punctuator("[") && tokens[at + 1].is_punctuator("]")))) {
        return at + 2;
    }
    if (at < tokens.size() && (tokens[at].is_identifier("new") || tokens[at].is_identifier("delete"))) {
        return at + 2 < tokens.size() && tokens[at + 1].is_punctuator("[") && tokens[at + 2].is_punctuator("]")
                   ? at + 3
                   : at + 1;
    }
    if (at < tokens.size() && tokens[at].kind == TokenKind::Punctuator && !tokens[at].is_punctuator("(")) {
        return at + 1;
    }
    std::size_t open = at;
    while (open < tokens.size() && tokens[open].kind != TokenKind::EndOfFile && !tokens[open].is_punctuator("(")) {
        ++open;
    }
    return open;
}

// Whether the operator-function-id at `name` names a conversion function: a
// type follows `operator`, rather than an operator symbol, `new`, `delete`,
// `co_await` or a literal operator's `""`.
bool conversion_function(const std::vector<Token>& tokens, std::size_t name) {
    if (name + 1 >= tokens.size()) {
        return false;
    }
    const Token& next = tokens[name + 1];
    return next.kind != TokenKind::Punctuator && next.kind != TokenKind::StringLiteral && !next.is_identifier("new") &&
           !next.is_identifier("delete") && !next.is_identifier("co_await");
}

// The name a declarator at `name` declares, as diagnostics spell it: the
// identifier, or an operator function's whole operator-function-id,
// `operator()`, `operator[]`, `operator+`.
std::string declarator_name_text(const std::vector<Token>& tokens, std::size_t name) {
    std::string text(tokens[name].text);
    const std::size_t end = std::min(declarator_parameters(tokens, name), tokens.size());
    for (std::size_t at = name + 1; at < end; ++at) {
        if (tokens[at].kind != TokenKind::Punctuator) {
            text += ' ';
        }
        text += tokens[at].text;
    }
    return text;
}

std::optional<std::size_t> find_declarator_name(const std::vector<Token>& tokens, std::size_t index) {
    std::size_t depth = 0;
    for (std::size_t cursor = index + 1; cursor < tokens.size(); ++cursor) {
        const Token& token = tokens[cursor];
        if (token.kind == TokenKind::EndOfFile) {
            break;
        }
        // An operator function is named by `operator` and the symbol after it,
        // whatever brackets that symbol holds.
        if (depth == 0 && token.is_identifier("operator")) {
            return cursor;
        }
        if (token.is_punctuator("(")) {
            if (depth == 0 && cursor > index + 1 && tokens[cursor - 1].kind == TokenKind::Identifier &&
                !is_type_keyword(tokens[cursor - 1]) && !is_one_of(tokens[cursor - 1], kNotDeclaratorNames)) {
                return cursor - 1;
            }
            // An explicit specialization's declarator carries its arguments
            // before the parameter list, so the name is what precedes them.
            if (depth == 0 && cursor > index + 1 && tokens[cursor - 1].is_punctuator(">")) {
                if (const auto open = template_arguments_start(tokens, cursor - 1);
                    open.has_value() && *open > index + 1 && tokens[*open - 1].kind == TokenKind::Identifier &&
                    !is_type_keyword(tokens[*open - 1])) {
                    return *open - 1;
                }
            }
            ++depth;
            continue;
        }
        if (token.is_punctuator(")")) {
            if (depth == 0) {
                break;
            }
            --depth;
            continue;
        }
        if (depth == 0 && (token.is_punctuator(";") || token.is_punctuator("{"))) {
            break;
        }
    }
    return std::nullopt;
}

// The first token of the qualified-id whose last component is `name`, so
// `C<int>::f` is taken whole rather than as its last component. A reference to
// the specialization has to name it the way the author did.
std::size_t qualified_name_start(const std::vector<Token>& tokens, std::size_t name) {
    std::size_t start = name;
    while (start >= 2 && tokens[start - 1].is_punctuator("::")) {
        std::size_t previous = start - 2;
        if (tokens[previous].is_punctuator(">")) {
            const std::optional<std::size_t> open = template_arguments_start(tokens, previous);
            if (!open.has_value() || *open == 0) {
                break;
            }
            previous = *open - 1;
        }
        if (tokens[previous].kind != TokenKind::Identifier) {
            break;
        }
        start = previous;
    }
    return start;
}

// The ordinary declarator - cv-qualifiers, ref-qualifiers, `noexcept`
// (optionally with a parenthesized operand), a trailing return type, and
// member markers such as `override`/`final` - stands between the parameter
// list and the first C++L clause (GRAMMAR.md 42-45: "Ordinary declarator,
// then C++L clauses, then body or semicolon"; C++L never splits it apart).
// This walks past exactly that stretch without needing to parse its grammar:
// it stops at the first token that begins a specification clause (a clause
// keyword immediately followed by '(') or at the body/semicolon that ends
// the declaration, keeping balanced parentheses (for `noexcept(expr)` and a
// trailing function-type return) skipped over rather than misread as a
// clause boundary.
//
// It also stops at a `,` that ends the declarator. What follows such a comma
// declares another name, so nothing after it is a clause of this one: in
// `int a(1), ensures(2);` the second declarator is an ordinary variable, and
// reading it as a clause would reinterpret valid C++ (SPEC.md WORD-008). A
// trailing return type's template arguments and an attribute list hold commas
// of their own, so their brackets are tracked; a `<` there is never a
// comparison, which could only stand inside parentheses.
//
// A `:` ends it too. It begins a constructor's mem-initializers, which belong to
// the body: in `explicit S(int a) : expects(a) {}` the member `expects` is
// initialized, and no clause is written. A trailing return type names its type
// before anything else, so a clause word there before any type, as in
// `auto f() -> ensures (&)[3]`, is that type. So is one that a `::` qualifies.
std::size_t skip_ordinary_declarator_suffix(const std::vector<Token>& tokens, std::size_t cursor) {
    std::size_t nesting = 0;
    bool awaiting_return_type = false;
    while (cursor < tokens.size()) {
        const Token& token = tokens[cursor];
        if (token.kind == TokenKind::EndOfFile || token.is_punctuator("{") || token.is_punctuator(";")) {
            break;
        }
        if (nesting == 0 && token.is_punctuator(",")) {
            break;
        }
        if (nesting == 0 && token.is_punctuator(":")) {
            break;
        }
        if (nesting == 0 && !awaiting_return_type && !tokens[cursor - 1].is_punctuator("::") &&
            is_specification_clause(token) && cursor + 1 < tokens.size() && tokens[cursor + 1].is_punctuator("(")) {
            break;
        }
        if (token.is_punctuator("->")) {
            awaiting_return_type = true;
        } else if (token.kind == TokenKind::Identifier &&
                   !is_one_of(token, std::to_array<std::string_view>(
                                         {"const", "volatile", "typename", "struct", "class", "enum", "union"}))) {
            awaiting_return_type = false;
        }
        if (token.is_punctuator("(")) {
            cursor = matching_parenthesis(tokens, cursor) + 1;
            continue;
        }
        if (token.is_punctuator("<") || token.is_punctuator("[")) {
            ++nesting;
        } else if ((token.is_punctuator(">") || token.is_punctuator("]")) && nesting > 0) {
            --nesting;
        } else if (token.is_punctuator(">>")) {
            nesting = nesting > 2 ? nesting - 2 : 0;
        }
        ++cursor;
    }
    return cursor;
}

} // namespace detail::recognizer

} // namespace cppl::frontend
