// Recognizing Laws and refinement types.

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
#include <vector>

namespace cppl::frontend {

namespace detail::recognizer {

bool try_law(const TokenStream& stream, std::size_t index, diagnostics::Engine& engine, LawDeclaration& law,
             std::size_t& next_index, ProofDeclaration& body, RecognitionMode mode) {
    const std::vector<Token>& tokens = stream.tokens();

    if (index + 2 >= tokens.size()) {
        return false;
    }
    if (tokens[index + 1].kind != TokenKind::Identifier || !tokens[index + 2].is_punctuator("(")) {
        return false;
    }

    const std::size_t close = matching_parenthesis(tokens, index + 2);
    if (close >= tokens.size()) {
        return false;
    }

    const bool draft = mode == RecognitionMode::Draft;
    const auto head = [&] {
        law.name = std::string(tokens[index + 1].text);
        law.name_location = stream.location_of(tokens[index + 1]);
        law.name_span = tokens[index + 1].span;
        law.range.begin = law.name_location;
        law.keyword_location = stream.location_of(tokens[index]);
        law.keyword = tokens[index].span;
        law.parameters =
            source::ByteSpan{tokens[index + 2].span.end(), tokens[close].span.offset - tokens[index + 2].span.end()};
    };

    std::size_t cursor = close + 1;
    if (cursor >= tokens.size() || !clause_kind(tokens[cursor]).has_value()) {
        // Still ordinary C++. A draft keeps where a clause would make it a Law.
        if (draft) {
            head();
            law.range.span = tokens_span(tokens[index], tokens[close]);
            law.completeness = Completeness::AwaitingClause;
        }
        return false;
    }

    head();

    bool malformed = false;
    while (cursor < tokens.size()) {
        const std::optional<ClauseKind> kind = clause_kind(tokens[cursor]);
        if (!kind.has_value()) {
            break;
        }
        if (cursor + 1 >= tokens.size() || !tokens[cursor + 1].is_punctuator("(")) {
            report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                   "'" + std::string(tokens[cursor].text) +
                       "' must be followed by a parenthesized specification expression");
            next_index = cursor + 1;
            return true;
        }

        const std::size_t clause_close = matching_parenthesis(tokens, cursor + 1);
        if (clause_close >= tokens.size()) {
            report(engine, stream, tokens[cursor + 1], diagnostics::Category::CpplSyntax,
                   "unterminated specification expression");
            next_index = cursor + 2;
            return true;
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
            malformed = true;
        }
        law.clauses.push_back(clause);
        if (*kind == ClauseKind::Decreases) {
            report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                   "a Law has only 'expects' and 'proves' clauses");
            malformed = true;
        }
        if (*kind == ClauseKind::Ensures) {
            report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                   "a Law conclusion uses 'proves', never 'ensures'",
                   "replace 'ensures' with 'proves'; a Law has no runtime result");
            malformed = true;
        }
        cursor = clause_close + 1;
    }

    check_clause_sequence(law.clauses, engine);
    const bool has_body = cursor < tokens.size() && tokens[cursor].is_punctuator("{");
    if (cursor >= tokens.size() || (!tokens[cursor].is_punctuator(";") && !has_body)) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "a law declaration ends with ';' or an explicit proof body");
        if (draft) {
            law.range.span = tokens_span(tokens[index], tokens[cursor - 1]);
            law.completeness = Completeness::AwaitingBody;
        }
        next_index = cursor;
        return true;
    }

    law.range.span = source::ByteSpan{tokens[index].span.offset, tokens[cursor].span.end() - tokens[index].span.offset};
    law.end_line = tokens[cursor].line;
    next_index = cursor + 1;

    if (has_body) {
        std::size_t end = matching_brace(tokens, cursor);
        if (end >= tokens.size()) {
            report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax, "unterminated Law proof body");
            if (!draft) {
                law.name.clear();
                return true;
            }
            // A draft reads the body to the end of the text, and reads on
            // inside it for whatever else is written there.
            end = tokens.size() - 1;
            law.completeness = Completeness::UnterminatedBody;
            body.completeness = Completeness::UnterminatedBody;
        }
        body.name = law.name;
        body.name_location = law.name_location;
        body.name_span = law.name_span;
        body.range = law.range;
        body.range.span = tokens_span(tokens[cursor], tokens[end]);
        body.body = body.range.span;
        body.keyword_location = law.keyword_location;
        body.parameters = law.parameters;
        body.end_line = tokens[end].line;
        if (const auto* conclusion = law.proposition()) {
            body.proposition = conclusion->expression;
            body.proposition_location = conclusion->location;
        }
        law.range.span.length = tokens[cursor].span.offset - law.range.span.offset;
        if (body.completeness == Completeness::UnterminatedBody) {
            read_block(stream, cursor, end, engine, body.statements, 0, true);
            next_index = cursor + 1;
            return true;
        }
        if (!read_block(stream, cursor, end, engine, body.statements, 0, draft) || body.statements.empty()) {
            report(engine, stream, tokens[cursor], diagnostics::Category::ProofFailure,
                   "a Law proof body must supply evidence closing its goal");
            malformed = true;
        }
        next_index = end + 1;
    }

    const auto ensures_count =
        std::ranges::count_if(law.clauses, [](const Clause& clause) { return clause.kind == ClauseKind::Proves; });
    if (ensures_count == 0) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "law '" + law.name + "' states no proposition", "a law requires exactly one proves clause");
        malformed = true;
    } else if (ensures_count > 1) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "law '" + law.name + "' has " + std::to_string(ensures_count) + " proves clauses",
               "a law has exactly one proves clause");
        malformed = true;
    }

    if (malformed && mode == RecognitionMode::Compile) {
        law.clauses.clear();
        law.name.clear();
    }
    return true;
}

// type name [(index parameters)] = base-type where (predicate);
//                                            (SPEC.md 17, 18; GRAMMAR.md 14, 16)
//
// `type` and `where` are contextual words (SPEC.md 3), so this is C++L only in
// the complete form. Anything else spelled `type` is an ordinary C++ identifier:
// `type x = 5;`, `type f(int);` and `using type = int;` all stay Clang's, and so
// does a `where` written anywhere else, including inside the base type's own
// brackets. Once the form is complete the declaration is C++L, and what is wrong
// with it is reported here rather than left to Clang.
bool try_refinement_type(const TokenStream& stream, std::size_t index, diagnostics::Engine& engine,
                         RefinementType& refinement, std::size_t& next_index) {
    const std::vector<Token>& tokens = stream.tokens();
    if (index + 2 >= tokens.size() || tokens[index + 1].kind != TokenKind::Identifier) {
        return false;
    }

    std::size_t cursor = index + 2;
    std::size_t indices_open = tokens.size();
    std::size_t indices_close = tokens.size();
    if (tokens[cursor].is_punctuator("(")) {
        indices_open = cursor;
        indices_close = matching_parenthesis(tokens, cursor);
        if (indices_close >= tokens.size()) {
            return false;
        }
        cursor = indices_close + 1;
    }
    if (cursor >= tokens.size() || !tokens[cursor].is_punctuator("=")) {
        return false;
    }
    const std::size_t equals = cursor;

    // The `where` that delimits the predicate stands at the declaration's own
    // level. One inside brackets belongs to the base type and is C++.
    std::size_t where = tokens.size();
    for (std::size_t scan = equals + 1; scan < tokens.size(); ++scan) {
        if (tokens[scan].is_punctuator("(") || tokens[scan].is_punctuator("[")) {
            const std::size_t close =
                tokens[scan].is_punctuator("(") ? matching_parenthesis(tokens, scan) : matching_bracket(tokens, scan);
            if (close >= tokens.size()) {
                return false;
            }
            scan = close;
        } else if (tokens[scan].is_punctuator("{") || tokens[scan].is_punctuator(";") ||
                   tokens[scan].kind == TokenKind::EndOfFile) {
            return false;
        } else if (tokens[scan].is_identifier("where")) {
            where = scan;
            break;
        }
    }
    if (where >= tokens.size()) {
        return false;
    }
    if (where + 1 >= tokens.size() || !tokens[where + 1].is_punctuator("(")) {
        return false; // `where` used as an ordinary name in the base type
    }
    // The predicate's `where` follows the base type, and a type-id ends only
    // in a name, a keyword, `>`, `*`, `&`, `&&`, `)` or `]`. After any other
    // punctuator, such as the `,` of `type a = 5, where (6);`, the word is an
    // operand of an initializer or the next declarator of a C++ declaration
    // (SPEC.md 3.1). Directly after `=` there is no base type at all, which is
    // refused below as what it is.
    if (const Token& before = tokens[where - 1];
        before.kind == TokenKind::Punctuator && !before.is_punctuator("=") && !before.is_punctuator(">") &&
        !before.is_punctuator(">>") && !before.is_punctuator("*") && !before.is_punctuator("&") &&
        !before.is_punctuator("&&") && !before.is_punctuator(")") && !before.is_punctuator("]")) {
        return false;
    }
    const std::size_t predicate_close = matching_parenthesis(tokens, where + 1);
    if (predicate_close >= tokens.size()) {
        report(engine, stream, tokens[where + 1], diagnostics::Category::CpplSyntax,
               "unterminated refinement predicate");
        next_index = where + 2;
        return true;
    }

    refinement.name = std::string(tokens[index + 1].text);
    refinement.name_span = tokens[index + 1].span;
    refinement.range.begin = stream.location_of(tokens[index + 1]);
    refinement.keyword_location = stream.location_of(tokens[index]);
    refinement.keyword = tokens[index].span;
    refinement.where_keyword = tokens[where].span;
    refinement.base_location = stream.location_of(tokens[equals + 1]);
    refinement.base =
        source::ByteSpan{tokens[equals].span.end(), tokens[where].span.offset - tokens[equals].span.end()};
    refinement.predicate = source::ByteSpan{tokens[where + 1].span.end(),
                                            tokens[predicate_close].span.offset - tokens[where + 1].span.end()};
    refinement.predicate_location = stream.location_of(tokens[where]);
    if (indices_open < tokens.size()) {
        refinement.indexed = true;
        refinement.indices = source::ByteSpan{tokens[indices_open].span.end(),
                                              tokens[indices_close].span.offset - tokens[indices_open].span.end()};
    }

    bool malformed = false;
    const auto blank = [&stream](const source::ByteSpan& span) {
        return stream.spelling(span).find_first_not_of(" \t\r\n") == std::string_view::npos;
    };
    if (blank(refinement.base)) {
        report(engine, stream, tokens[equals], diagnostics::Category::CpplSyntax,
               "refinement type '" + refinement.name + "' declares no base type",
               "a refinement restricts the values of an ordinary C++ type: 'type " + refinement.name +
                   " = int where (...)'");
        malformed = true;
    }
    if (blank(refinement.predicate)) {
        report(engine, stream, tokens[where], diagnostics::Category::CpplSyntax,
               "refinement type '" + refinement.name + "' states no predicate",
               "'where' requires a specification expression over 'self'");
        malformed = true;
    }
    if (refinement.indexed && blank(refinement.indices)) {
        report(engine, stream, tokens[indices_open], diagnostics::Category::CpplSyntax,
               "refinement type '" + refinement.name + "' declares an empty index list",
               "write the indices the predicate uses, or no parentheses at all");
        malformed = true;
    }

    std::size_t end = predicate_close + 1;
    if (end >= tokens.size() || !tokens[end].is_punctuator(";")) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "a refinement type declaration ends with ';'");
        next_index = end;
        return true;
    }
    refinement.range.span =
        source::ByteSpan{tokens[index].span.offset, tokens[end].span.end() - tokens[index].span.offset};
    refinement.end_line = tokens[end].line;
    next_index = end + 1;

    if (malformed) {
        refinement.name.clear();
    }
    return true;
}

} // namespace detail::recognizer

} // namespace cppl::frontend
