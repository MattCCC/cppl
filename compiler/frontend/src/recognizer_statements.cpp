// Scopes, loop clauses, and the ends of the statements a C++L word may lead:
// claims, ghost declarations and case splits.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "recognizer_state.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::frontend {

using detail::recognizer::matching_bracket;

namespace detail::recognizer {

// Classifies the scope a '{' opens by looking back at the declaration it
// belongs to. C++L declarations are only recognized at namespace scope in this
// implementation; elsewhere they are diagnosed rather than half-handled
// (GRAMMAR.md 36).
ScopeKind scope_kind_before(const std::vector<Token>& tokens, std::size_t brace) {
    // A linkage specification, `extern "C" { ... }`, gives what it encloses a
    // language linkage and opens no scope: a declaration in it stands at the
    // namespace scope around it (C++ [dcl.link]).
    if (brace >= 2 && tokens[brace - 1].kind == TokenKind::StringLiteral && tokens[brace - 2].is_identifier("extern")) {
        return ScopeKind::Namespace;
    }
    for (std::size_t cursor = brace; cursor > 0; --cursor) {
        const Token& token = tokens[cursor - 1];
        if (token.is_punctuator(";") || token.is_punctuator("{") || token.is_punctuator("}")) {
            break;
        }
        if (token.is_identifier("namespace")) {
            return ScopeKind::Namespace;
        }
        if (token.is_identifier("class") || token.is_identifier("struct") || token.is_identifier("union") ||
            token.is_identifier("enum")) {
            return ScopeKind::Class;
        }
        if (token.is_punctuator(")")) {
            return ScopeKind::Block;
        }
    }
    return ScopeKind::Block;
}

// The name a class scope's '{' belongs to: the identifier after the `class`,
// `struct` or `union` its declaration begins with, or nothing for an unnamed
// class. What a constructor or a destructor of that class is named.
std::string_view class_name_before(const std::vector<Token>& tokens, std::size_t brace) {
    for (std::size_t cursor = brace; cursor > 0; --cursor) {
        const Token& token = tokens[cursor - 1];
        if (token.is_punctuator(";") || token.is_punctuator("{") || token.is_punctuator("}")) {
            break;
        }
        if (token.is_identifier("class") || token.is_identifier("struct") || token.is_identifier("union")) {
            return cursor < brace && tokens[cursor].kind == TokenKind::Identifier ? tokens[cursor].text
                                                                                  : std::string_view{};
        }
    }
    return {};
}

} // namespace detail::recognizer

namespace {

bool is_loop_clause(const std::vector<Token>& tokens, std::size_t index) {
    return index + 1 < tokens.size() &&
           (tokens[index].is_identifier("invariant") || tokens[index].is_identifier("decreases")) &&
           tokens[index + 1].is_punctuator("(");
}

// Whether the tokens strictly between `open` and `close` form a declarator a
// declaration could name in parentheses: pointer and reference operators and
// cv-qualifiers, one name, and array bounds, as in `(x)`, `(*p)` or `(a[2])`.
bool parenthesized_declarator(const std::vector<Token>& tokens, std::size_t open, std::size_t close) {
    std::size_t cursor = open + 1;
    while (cursor < close && (tokens[cursor].is_punctuator("*") || tokens[cursor].is_punctuator("&") ||
                              tokens[cursor].is_punctuator("&&") || tokens[cursor].is_identifier("const") ||
                              tokens[cursor].is_identifier("volatile"))) {
        ++cursor;
    }
    if (cursor >= close || tokens[cursor].kind != TokenKind::Identifier) {
        return false;
    }
    ++cursor;
    while (cursor < close && tokens[cursor].is_punctuator("[")) {
        cursor = matching_bracket(tokens, cursor) + 1;
    }
    return cursor == close;
}

} // namespace

namespace detail::recognizer {

// Whether the braces from `open` to `close` hold a `;` of their own, outside
// any nested bracket. A compound statement holding a statement does; a
// braced-init-list never does.
bool holds_a_statement(const std::vector<Token>& tokens, std::size_t open, std::size_t close) {
    std::size_t depth = 0;
    for (std::size_t cursor = open + 1; cursor < close; ++cursor) {
        const Token& token = tokens[cursor];
        if (token.is_punctuator("(") || token.is_punctuator("[") || token.is_punctuator("{")) {
            ++depth;
        } else if ((token.is_punctuator(")") || token.is_punctuator("]") || token.is_punctuator("}")) && depth > 0) {
            --depth;
        } else if (depth == 0 && token.is_punctuator(";")) {
            return true;
        }
    }
    return false;
}

// `while (c)`, `for (...)` or `do` followed by loop specification clauses and
// a block (GRAMMAR.md 25, 26). The clauses are C++L only where the tokens
// cannot be C++ (SPEC.md 3.1). `invariant (x) {n};` and `decreases (k) {n},
// (j) {m};` declare locals with braced initializers wherever the word names a
// type, so one clause holding a declarator, before braces that hold no
// statement and that `;` or `,` follows, is left to C++.
// `clause_start` is where loop clauses may begin: just past the condition's
// ')' for `while (c)`/`for (...)`, or right after the keyword itself for
// `do` (GRAMMAR.md 26: `"do" loop-clauses compound-statement "while" ...` -
// `do` has no leading `(condition)` for the clauses to follow).
LoopClauses try_loop_clauses(const TokenStream& stream, std::size_t index, std::size_t clause_start,
                             diagnostics::Engine& engine, LoopSpecification& loop, std::size_t& next_index) {
    const std::vector<Token>& tokens = stream.tokens();
    if (clause_start >= tokens.size() || !is_loop_clause(tokens, clause_start)) {
        return LoopClauses::None;
    }

    struct Written {
        std::size_t keyword;
        std::size_t close;
    };
    std::vector<Written> written;
    std::size_t cursor = clause_start;
    while (is_loop_clause(tokens, cursor)) {
        const std::size_t clause_close = matching_parenthesis(tokens, cursor + 1);
        if (clause_close >= tokens.size()) {
            return LoopClauses::None;
        }
        written.push_back(Written{cursor, clause_close});
        cursor = clause_close + 1;
    }
    if (cursor >= tokens.size() || !tokens[cursor].is_punctuator("{")) {
        return LoopClauses::None;
    }
    const std::size_t body_close = matching_brace(tokens, cursor);
    if (body_close >= tokens.size()) {
        return LoopClauses::None;
    }
    const bool declaration_follows = body_close + 1 < tokens.size() && (tokens[body_close + 1].is_punctuator(";") ||
                                                                        tokens[body_close + 1].is_punctuator(","));
    if (written.size() == 1 && declaration_follows &&
        parenthesized_declarator(tokens, written[0].keyword + 1, written[0].close) &&
        !holds_a_statement(tokens, cursor, body_close)) {
        return LoopClauses::None;
    }

    next_index = cursor;
    LoopClauses outcome = LoopClauses::Recognized;
    for (const Written& clause : written) {
        const Token& keyword = tokens[clause.keyword];
        if (keyword.is_identifier("decreases")) {
            if (loop.decreases.has_value()) {
                report(engine, stream, keyword, diagnostics::Category::CpplSyntax,
                       "a loop states one 'decreases' clause",
                       "a lexicographic measure is one clause with its parts separated by ','");
                outcome = LoopClauses::Refused;
                continue;
            }
            const source::ByteSpan measure{tokens[clause.keyword + 1].span.end(),
                                           tokens[clause.close].span.offset - tokens[clause.keyword + 1].span.end()};
            if (stream.spelling(measure).find_first_not_of(" \t\r\n") == std::string_view::npos) {
                report(engine, stream, keyword, diagnostics::Category::CpplSyntax,
                       "'decreases' requires an expression");
                outcome = LoopClauses::Refused;
                continue;
            }
            // A lexicographic list is one measure per component (SPEC.md 22.3,
            // TERMINATION-004); each component must be an expression.
            const Clause decreases{ClauseKind::Decreases, keyword.span, measure, stream.location_of(keyword)};
            if (std::ranges::any_of(measure_components(stream, decreases), [](const MeasureComponent& component) {
                    return component.expression.length == 0;
                })) {
                report(engine, stream, keyword, diagnostics::Category::CpplSyntax,
                       "each component of a 'decreases' list is an expression",
                       "a lexicographic measure separates its components with ','");
                outcome = LoopClauses::Refused;
                continue;
            }
            loop.decreases = decreases;
            continue;
        }
        Clause invariant;
        invariant.kind = ClauseKind::Invariant;
        invariant.keyword = keyword.span;
        invariant.location = stream.location_of(keyword);
        invariant.expression =
            source::ByteSpan{tokens[clause.keyword + 1].span.end(),
                             tokens[clause.close].span.offset - tokens[clause.keyword + 1].span.end()};
        if (clause.close == clause.keyword + 2) {
            report(engine, stream, keyword, diagnostics::Category::CpplSyntax, "'invariant' requires an expression");
            outcome = LoopClauses::Refused;
            continue;
        }
        loop.invariants.push_back(invariant);
        loop.expression_locations.push_back(stream.location_of(tokens[clause.keyword + 2]));
    }

    check_clause_sequence(loop.invariants, engine);
    loop.keyword = tokens[index].span;
    loop.keyword_location = stream.location_of(tokens[index]);
    loop.clause_region = source::ByteSpan{tokens[written.front().keyword].span.offset,
                                          tokens[cursor].span.offset - tokens[written.front().keyword].span.offset};
    loop.body_open = tokens[cursor].span.end();
    loop.body_open_line = tokens[cursor].line;
    loop.body_open_column = tokens[cursor].column + 1;
    return outcome;
}

// Where a `contradiction` statement beginning at `index` ends: the index of its
// `;`, when the tokens have the statement's one shape, `contradiction name;` or
// `contradiction name(arguments);` (GRAMMAR.md 5.6).
std::optional<std::size_t> contradiction_statement_end(const std::vector<Token>& tokens, std::size_t index) {
    if (index + 2 >= tokens.size() || tokens[index + 1].kind != TokenKind::Identifier) {
        return std::nullopt;
    }
    if (tokens[index + 2].is_punctuator(";")) {
        return index + 2;
    }
    if (!tokens[index + 2].is_punctuator("(")) {
        return std::nullopt;
    }
    const std::size_t close = matching_parenthesis(tokens, index + 2);
    if (close + 1 >= tokens.size() || !tokens[close + 1].is_punctuator(";")) {
        return std::nullopt;
    }
    return close + 1;
}

// Where a `ghost` declaration beginning at `index` ends: the index of its `;`,
// when a declaration follows the word (GRAMMAR.md 21). Whether it declares
// anything is Clang's to say; this only delimits it. `ghost::` begins a
// qualified name, never a declaration (`names_a_scope`).
std::optional<std::size_t> ghost_declaration_end(const std::vector<Token>& tokens, std::size_t index) {
    if (index + 1 >= tokens.size() || tokens[index + 1].kind != TokenKind::Identifier) {
        return std::nullopt;
    }
    std::size_t depth = 0;
    for (std::size_t cursor = index + 1; cursor < tokens.size() && tokens[cursor].kind != TokenKind::EndOfFile;
         ++cursor) {
        const Token& token = tokens[cursor];
        if (token.is_punctuator("(") || token.is_punctuator("[") || token.is_punctuator("{")) {
            ++depth;
        } else if (token.is_punctuator(")") || token.is_punctuator("]") || token.is_punctuator("}")) {
            if (depth == 0) {
                return std::nullopt;
            }
            --depth;
        } else if (depth == 0 && token.is_punctuator(";")) {
            return cursor;
        }
    }
    return std::nullopt;
}

// Whether a ghost declaration names both a type and a variable before its
// initializer: `ghost x = y;` would leave the assignment `x = y;` behind the
// word, not a declaration.
bool ghost_declares(const std::vector<Token>& tokens, std::size_t index, std::size_t end) {
    std::size_t names = 0;
    for (std::size_t cursor = index + 1; cursor < end; ++cursor) {
        const Token& token = tokens[cursor];
        if (token.is_punctuator("=") || token.is_punctuator("(") || token.is_punctuator("{")) {
            break;
        }
        if (token.kind == TokenKind::Identifier) {
            ++names;
        }
    }
    return names >= 2;
}

// Where a `cases` or `decompose` statement beginning at `index` ends: the index
// of the `}` closing its arms, when the tokens have the statement's one shape, a
// subject and then a braced arm list (GRAMMAR.md 5.7). `cases::` begins a
// qualified name, never a split (`names_a_scope`).
std::optional<std::size_t> split_statement_end(const std::vector<Token>& tokens, std::size_t index) {
    if (names_a_scope(tokens, index)) {
        return std::nullopt;
    }
    std::size_t depth = 0;
    std::size_t open = index + 1;
    for (; open < tokens.size() && tokens[open].kind != TokenKind::EndOfFile; ++open) {
        const Token& token = tokens[open];
        if (token.is_punctuator("(") || token.is_punctuator("[")) {
            ++depth;
        } else if (token.is_punctuator(")") || token.is_punctuator("]")) {
            if (depth == 0) {
                return std::nullopt;
            }
            --depth;
        } else if (depth == 0 && (token.is_punctuator(";") || token.is_punctuator("}"))) {
            return std::nullopt;
        } else if (depth == 0 && token.is_punctuator("{")) {
            break;
        }
    }
    if (open >= tokens.size() || open == index + 1 || !tokens[open].is_punctuator("{")) {
        return std::nullopt;
    }
    const std::size_t close = matching_brace(tokens, open);
    if (close >= tokens.size()) {
        return std::nullopt;
    }
    return close;
}

// An arm of a split on a runtime path continues that path, so there is no goal
// in it for `refl`, `exact`, `apply`, `assume` or `rewrite` to close. It holds
// what can stand on a path: a nested split, and a `contradiction` claiming the
// path cannot occur, which ends it (SPEC.md CASE-017, VERIFIED-045).
bool admit_split_arms(diagnostics::Engine& engine, const ProofStatement& statement) {
    for (const ProofArm& arm : statement.arms) {
        if (arm.omitted) {
            continue;
        }
        for (std::size_t index = 0; index < arm.statements.size(); ++index) {
            const ProofStatement& inner = arm.statements[index];
            if (inner.kind == ProofStatementKind::Cases || inner.kind == ProofStatementKind::Decompose) {
                if (!admit_split_arms(engine, inner)) {
                    return false;
                }
                continue;
            }
            if (inner.kind == ProofStatementKind::Contradiction) {
                if (index + 1 != arm.statements.size()) {
                    report(engine, arm.statements[index + 1].location, diagnostics::Category::CpplSyntax,
                           "nothing after a contradiction in this arm is reached",
                           "a contradiction ends the path it is written on");
                    return false;
                }
                continue;
            }
            report(engine, inner.location, diagnostics::Category::CpplSyntax,
                   "'" + describe(inner.kind) + "' has no goal to close in a case split on a runtime path",
                   "an arm of a split in a verified body continues its path, so it holds only a nested 'cases' "
                   "or 'decompose' and a 'contradiction' that ends the path");
            return false;
        }
    }
    return true;
}

// The claims a split's arms hold, nested splits' included, in the order a walk
// of arms and their statements meets them. The projector walks the same order.
void split_claims(const ProofStatement& statement,
                  std::vector<std::pair<const ProofStatement*, const ProofArm*>>& found) {
    for (const ProofArm& arm : statement.arms) {
        for (const ProofStatement& inner : arm.statements) {
            if (inner.kind == ProofStatementKind::Contradiction) {
                found.emplace_back(&inner, arm.omitted ? &arm : nullptr);
            } else {
                split_claims(inner, found);
            }
        }
    }
}

// The first module import of the unit, `import name;`, `import :part;` or
// `import <header>;`, exported or not, standing at the top level where a
// declaration begins (SPEC.md MODULE-001).
std::optional<std::size_t> first_module_import(const std::vector<Token>& tokens) {
    std::size_t depth = 0;
    for (std::size_t at = 0; at + 1 < tokens.size(); ++at) {
        const Token& token = tokens[at];
        if (token.is_punctuator("{")) {
            ++depth;
        } else if (token.is_punctuator("}") && depth > 0) {
            --depth;
        } else if (depth == 0 && token.is_identifier("import")) {
            const bool begins = at == 0 || tokens[at - 1].is_punctuator(";") || tokens[at - 1].is_punctuator("}") ||
                                tokens[at - 1].is_identifier("export");
            const Token& next = tokens[at + 1];
            if (begins && (next.kind == TokenKind::Identifier || next.is_punctuator(":") || next.is_punctuator("<") ||
                           next.kind == TokenKind::StringLiteral)) {
                return at;
            }
        }
    }
    return std::nullopt;
}

// Whether a statement can begin at `index`: what precedes it ends a statement
// or opens a block or a statement's body, and no parenthesis is open around it,
// as one is in a `for` header.
bool at_statement_start(const std::vector<Token>& tokens, std::size_t index) {
    if (index == 0) {
        return false;
    }
    const Token& previous = tokens[index - 1];
    if (!previous.is_punctuator("{") && !previous.is_punctuator("}") && !previous.is_punctuator(";") &&
        !previous.is_punctuator(":") && !previous.is_punctuator(")") && !previous.is_identifier("else") &&
        !previous.is_identifier("do")) {
        return false;
    }
    std::size_t closed = 0;
    for (std::size_t cursor = index; cursor > 0; --cursor) {
        const Token& token = tokens[cursor - 1];
        if (token.is_punctuator("{") || token.is_punctuator("}")) {
            return true;
        }
        if (token.is_punctuator(")")) {
            ++closed;
        } else if (token.is_punctuator("(")) {
            if (closed == 0) {
                return false;
            }
            --closed;
        }
    }
    return false;
}

} // namespace detail::recognizer

} // namespace cppl::frontend
