// Recognizing verified functions and their clauses, explicit
// instantiations, and the member functions refused by name.

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

namespace detail::recognizer {

// `template T f<args>(params);` at namespace scope: an explicit instantiation
// definition of a function template (SPEC.md TEMPLATE-001).
//
// Recognizing this is textual and deliberately narrow. `template <` introduces
// a template rather than instantiating one; `extern template` instantiates
// nothing here; and a class instantiation, `template struct C<int>;`, has no
// parameter list, so no declarator name is found. Whether the name resolves,
// and to which specialization, stays Clang's decision.
bool try_explicit_instantiation(const TokenStream& stream, const std::vector<Token>& tokens, std::size_t index,
                                ExplicitInstantiation& instantiation, std::size_t& next_index) {
    if (!tokens[index].is_identifier("template") || index + 1 >= tokens.size() ||
        tokens[index + 1].is_punctuator("<")) {
        return false;
    }
    if (index > 0 && tokens[index - 1].is_identifier("extern")) {
        return false;
    }
    const std::optional<std::size_t> name = find_declarator_name(tokens, index);
    if (!name.has_value()) {
        return false;
    }
    // The declaration ends at the first `;` outside any bracket. A definition
    // would have a body instead, which is not an explicit instantiation.
    std::size_t depth = 0;
    std::size_t end = tokens.size();
    for (std::size_t cursor = *name; cursor < tokens.size(); ++cursor) {
        const Token& token = tokens[cursor];
        if (token.is_punctuator("(") || token.is_punctuator("[")) {
            ++depth;
        } else if (token.is_punctuator(")") || token.is_punctuator("]")) {
            if (depth == 0) {
                return false;
            }
            --depth;
        } else if (depth == 0 && token.is_punctuator("{")) {
            return false;
        } else if (depth == 0 && token.is_punctuator(";")) {
            end = cursor;
            break;
        }
    }
    if (end == tokens.size()) {
        return false;
    }
    // The id-expression runs from the qualified name to the parameter list.
    std::size_t parameters = tokens.size();
    for (std::size_t cursor = *name; cursor < end; ++cursor) {
        if (tokens[cursor].is_punctuator("(")) {
            parameters = cursor;
            break;
        }
    }
    if (parameters == tokens.size()) {
        return false;
    }
    const std::size_t start = qualified_name_start(tokens, *name);
    instantiation.function_name = declarator_name_text(tokens, *name);
    instantiation.location = stream.location_of(tokens[*name]);
    instantiation.id_expression =
        source::ByteSpan{tokens[start].span.offset, tokens[parameters].span.offset - tokens[start].span.offset};
    instantiation.insertion_offset = tokens[end].span.end();
    instantiation.insertion_line = tokens[end].line;
    next_index = end + 1;
    return true;
}

// Specification clauses on a function declarator are part of the language
// (GRAMMAR.md 6) but are not verified by this implementation. They are
// diagnosed rather than erased, because silently dropping a contract would
// turn a specification into nothing at all.
//
// A clause stands only where the declarator's ordinary suffix ends, so only
// that one position is examined.
bool has_specification_clause(const std::vector<Token>& tokens, std::size_t name_index, std::size_t& clause_index) {
    const std::size_t open = declarator_parameters(tokens, name_index);
    if (open >= tokens.size() || !tokens[open].is_punctuator("(")) {
        return false;
    }
    const std::size_t close = matching_parenthesis(tokens, open);
    if (close >= tokens.size()) {
        return false;
    }

    const std::size_t cursor = skip_ordinary_declarator_suffix(tokens, close + 1);
    if (cursor + 1 < tokens.size() && is_specification_clause(tokens[cursor]) &&
        tokens[cursor + 1].is_punctuator("(")) {
        clause_index = cursor;
        return true;
    }
    return false;
}

// Reads `expects`/`ensures`/`decreases` clauses starting at `cursor` into
// `clauses`, exactly as GRAMMAR.md 6 orders them, stopping at the first token
// that is not a recognized clause keyword. Shared between `try_verified` and
// the Edit-mode-only formatter layout path below for a `pure` function with
// clauses this implementation does not check (`has_specification_clause`):
// both need the identical clause grammar, just gated by a different
// acceptance rule afterward.
std::optional<std::size_t> scan_function_clauses(const TokenStream& stream, std::size_t cursor,
                                                 diagnostics::Engine& engine, std::vector<Clause>& clauses) {
    const std::vector<Token>& tokens = stream.tokens();
    while (cursor < tokens.size()) {
        const std::optional<ClauseKind> kind = clause_kind(tokens[cursor]);
        if (!kind.has_value()) {
            break;
        }
        if (cursor + 1 >= tokens.size() || !tokens[cursor + 1].is_punctuator("(")) {
            report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                   "'" + std::string(tokens[cursor].text) +
                       "' must be followed by a parenthesized specification expression");
            return std::nullopt;
        }
        const std::size_t clause_close = matching_parenthesis(tokens, cursor + 1);
        if (clause_close >= tokens.size()) {
            report(engine, stream, tokens[cursor + 1], diagnostics::Category::CpplSyntax,
                   "unterminated specification expression");
            return std::nullopt;
        }

        Clause clause;
        clause.kind = *kind;
        clause.keyword = tokens[cursor].span;
        clause.location = stream.location_of(tokens[cursor]);
        clause.expression = source::ByteSpan{tokens[cursor + 1].span.end(),
                                             tokens[clause_close].span.offset - tokens[cursor + 1].span.end()};
        if (stream.spelling(clause.expression).find_first_not_of(" \t\r\n") == std::string_view::npos) {
            report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                   "'" + std::string(tokens[cursor].text) + "' requires an expression");
            return std::nullopt;
        }
        clauses.push_back(clause);
        // A lexicographic list is one measure per component (SPEC.md
        // TERMINATION-004); each component must be an expression.
        if (*kind == ClauseKind::Decreases &&
            std::ranges::any_of(measure_components(stream, clause),
                                [](const MeasureComponent& component) { return component.expression.length == 0; })) {
            report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                   "each component of a 'decreases' list is an expression",
                   "a lexicographic measure separates its components with ','");
            return std::nullopt;
        }
        if (*kind == ClauseKind::Proves) {
            report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                   "a runtime function postcondition uses 'ensures', never 'proves'");
            return std::nullopt;
        }
        cursor = clause_close + 1;
    }
    return cursor;
}

// Whether a C++L declaration keyword stands between the declaration's first
// token and its declarator name. Each of those has its own branch above that
// reads the declaration properly, so the layout-only fallback must leave them
// alone rather than file a second, overlapping region for the same text.
// A `law`/`proof`/`trusted` declaration reaches the fallback whenever its own
// branch declined it -- a non-exhaustive `cases`, say -- and its `proves`
// clause is a proposition, not a function postcondition. Reporting on one
// would replace that branch's real diagnostic with a misleading "a runtime
// function postcondition uses 'ensures', never 'proves'".
bool has_cppl_keyword(const std::vector<Token>& tokens, std::size_t index, std::size_t name_index) {
    for (std::size_t cursor = index; cursor < name_index && cursor < tokens.size(); ++cursor) {
        if (is_specifier(tokens, cursor, "verified") || is_specifier(tokens, cursor, "pure") ||
            is_specifier(tokens, cursor, "law") || is_specifier(tokens, cursor, "proof") ||
            is_specifier(tokens, cursor, "trusted")) {
            return true;
        }
    }
    return false;
}

// Records a clause-bearing function declaration for layout alone, where the
// declaration is not one this implementation checks: a `pure` function, or a
// function led by any other specifier (`inline`, `static`, `constexpr`, or a
// macro that expands to one). See `Syntax::unchecked_clauses`.
//
// `keyword_index` is the declaration's own first token, which is what the
// formatter indents the clause block against; `name_index` is the declarator
// name. Returns false when no clause survives the scan, leaving the caller to
// treat the declaration as ordinary C++.
bool record_unchecked_clauses(const TokenStream& stream, std::size_t keyword_index, std::size_t name_index,
                              diagnostics::Engine& engine, Syntax& syntax) {
    const std::vector<Token>& tokens = stream.tokens();
    const std::size_t open = declarator_parameters(tokens, name_index);
    const std::size_t close = matching_parenthesis(tokens, open);
    if (close >= tokens.size()) {
        return false;
    }
    const std::size_t first_clause = skip_ordinary_declarator_suffix(tokens, close + 1);
    VerifiedFunction layout;
    const std::optional<std::size_t> scanned = scan_function_clauses(stream, first_clause, engine, layout.clauses);
    if (!scanned.has_value() || layout.clauses.empty()) {
        return false;
    }
    layout.keyword = tokens[keyword_index].span;
    layout.keyword_location = stream.location_of(tokens[keyword_index]);
    layout.function_name = std::string(tokens[name_index].text);
    layout.function_location = stream.location_of(tokens[name_index]);
    layout.function_offset = tokens[name_index].span.offset;
    layout.parameters = source::ByteSpan{tokens[open].span.end(), tokens[close].span.offset - tokens[open].span.end()};
    layout.clause_region = source::ByteSpan{tokens[first_clause].span.offset,
                                            tokens[*scanned].span.offset - tokens[first_clause].span.offset};
    syntax.unchecked_clauses.push_back(std::move(layout));
    return true;
}

// Whether a token between `from` and `to` is the word `word`.
bool written_between(const std::vector<Token>& tokens, std::size_t from, std::size_t to, std::string_view word) {
    for (std::size_t cursor = from; cursor < to && cursor < tokens.size(); ++cursor) {
        if (tokens[cursor].is_identifier(word)) {
            return true;
        }
    }
    return false;
}

// A member function this implementation does not verify, refused where it is
// written rather than recognized and left unchecked. A virtual function's body
// is not what a call through its base interface runs, so a contract proved
// from it would say nothing about the call (SPEC.md CONTRACT-014, CLASS-014).
// A constructor and a destructor begin and end an object's lifetime, which the
// receiver model does not follow (SPEC.md CONTRACT-011, CONTRACT-012,
// CLASS-015). A member function template's probes would have to be forced at
// every specialization of a member, which nothing here does (CLASS-015).
// Whether `verified` on the member at `index`, whose declarator name is `name`,
// is one of those, and if so the diagnostic has been reported.
void refuse_lifetime_member(const TokenStream& stream, const Token& at, bool destructor, diagnostics::Engine& engine) {
    report(engine, stream, at, diagnostics::Category::UnsupportedSemantics,
           std::string("a verified ") + (destructor ? "destructor" : "constructor") +
               " is not verified by this implementation",
           "construction and destruction begin and end the object's lifetime, which the receiver model does not "
           "follow; verify the member functions that run on the constructed object instead (SPEC.md CONTRACT-011, "
           "CONTRACT-012, CLASS-015)");
}

bool refused_member(const TokenStream& stream, std::size_t index, std::size_t name, std::size_t close,
                    std::string_view class_name, diagnostics::Engine& engine) {
    const std::vector<Token>& tokens = stream.tokens();
    const std::size_t declaration_start = specifiers_start(tokens, index);
    if (template_header_start(tokens, declaration_start).has_value()) {
        report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
               "a verified member function template is not verified by this implementation",
               "its contract would have to be checked at every specialization of the member, which nothing forces "
               "here; state it on a non-template member (SPEC.md CLASS-015)");
        return true;
    }
    const bool destructor = name > 0 && tokens[name - 1].is_punctuator("~");
    std::size_t type_start = index + 1;
    if (is_specifier(tokens, type_start, "pure")) {
        ++type_start;
    }
    if (destructor || type_start >= name || (!class_name.empty() && tokens[name].is_identifier(class_name))) {
        refuse_lifetime_member(stream, tokens[index], destructor, engine);
        return true;
    }
    const std::size_t suffix_end = skip_ordinary_declarator_suffix(tokens, close + 1);
    if (written_between(tokens, declaration_start, name, "virtual") ||
        written_between(tokens, close + 1, suffix_end, "override") ||
        written_between(tokens, close + 1, suffix_end, "final")) {
        report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
               "a verified virtual function is not verified by this implementation",
               "a call through the base interface runs whichever override the dynamic type selects, so a contract "
               "proved from one body says nothing about the call; override substitutability is not checked here "
               "(SPEC.md CONTRACT-014, CLASS-006, CLASS-014)");
        return true;
    }
    return false;
}

} // namespace detail::recognizer

} // namespace cppl::frontend
