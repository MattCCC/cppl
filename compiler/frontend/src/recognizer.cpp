#include "cppl/decomposition/labels.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "recognition.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::frontend {

namespace {

constexpr std::array<std::string_view, 23> kTypeKeywords = {
    "void",     "bool",     "char",   "char8_t", "char16_t", "char32_t", "wchar_t",  "short",
    "int",      "long",     "float",  "double",  "signed",   "unsigned", "auto",     "const",
    "volatile", "typename", "struct", "class",   "enum",     "decltype", "constexpr"};

bool is_type_keyword(const Token& token) {
    return token.kind == TokenKind::Identifier && std::ranges::find(kTypeKeywords, token.text) != kTypeKeywords.end();
}

// Skips back over one `[[ ... ]]` attribute-specifier-seq element (two
// adjacent ']' immediately before `index`, matched back to their two
// adjacent '['), returning `index` unchanged if the tokens before it are not
// exactly that shape. `[[` lexes as two ordinary '[' tokens (this lexer has
// no digraph-style attribute token), so this matches bracket pairs, not a
// single punctuator.
std::size_t skip_back_over_attribute(const std::vector<Token>& tokens, std::size_t index) {
    if (index < 2 || !tokens[index - 1].is_punctuator("]") || !tokens[index - 2].is_punctuator("]")) {
        return index;
    }
    // Depth-balances every '[' against every ']' back to the pair started by
    // the outer '[' of '[[': since the attribute's two ']' both precede
    // `index`, a balanced scan naturally consumes both bracket pairs and
    // stops exactly at the outer '['.
    std::size_t depth = 0;
    std::size_t scan = index - 1;
    while (true) {
        if (tokens[scan].is_punctuator("]")) {
            ++depth;
        } else if (tokens[scan].is_punctuator("[")) {
            --depth;
            if (depth == 0) {
                break;
            }
        }
        if (scan == 0) {
            return index; // unbalanced: not an attribute after all
        }
        --scan;
    }
    return scan;
}

// The first token of the declaration at `index`, walking back over the ordinary
// specifiers and `[[...]]` attributes (GRAMMAR.md 40: "Ordinary attributes keep
// their C++ placement") that may precede a C++L declaration. Either kind may
// repeat and they may appear in either relative order, so both are skipped back
// over together until neither applies any more.
std::size_t specifiers_start(const std::vector<Token>& tokens, std::size_t index) {
    while (index > 0) {
        if (tokens[index - 1].is_identifier("static") || tokens[index - 1].is_identifier("inline") ||
            tokens[index - 1].is_identifier("constexpr") || tokens[index - 1].is_identifier("consteval") ||
            tokens[index - 1].is_identifier("virtual") || tokens[index - 1].is_identifier("extern")) {
            --index;
            continue;
        }
        const std::size_t after_attribute = skip_back_over_attribute(tokens, index);
        if (after_attribute != index) {
            index = after_attribute;
            continue;
        }
        break;
    }
    return index;
}

// The `template` token of the header introducing the declaration at `index`,
// where there is one (GRAMMAR.md 39: "C++ owns template syntax"). The header
// precedes the declaration it introduces, e.g. `template <typename T>\nverified
// ...`, so this scans back over one balanced `template < ... >`.
//
// The span matters beyond recognition: a contract probe for a templated
// function names the template's parameters, so it has to be emitted under the
// same header the author wrote (SPEC.md 42).
std::optional<std::size_t> template_header_start(const std::vector<Token>& tokens, std::size_t index) {
    if (index == 0 || !tokens[index - 1].is_punctuator(">")) {
        return std::nullopt;
    }
    std::size_t depth = 0;
    std::size_t scan = index - 1;
    std::size_t matched = tokens.size(); // the '<' balancing the initial '>'
    while (true) {
        if (tokens[scan].is_punctuator(">")) {
            ++depth;
        } else if (tokens[scan].is_punctuator("<")) {
            --depth;
            if (depth == 0) {
                matched = scan;
                break;
            }
        }
        if (scan == 0) {
            break; // no matching '<': not a template header
        }
        --scan;
    }
    if (matched < tokens.size() && matched > 0 && tokens[matched - 1].is_identifier("template")) {
        return matched - 1;
    }
    return std::nullopt;
}

// A declaration can begin here: at the start of the unit, or after a token that
// can only end a previous declaration, statement or label.
bool at_declaration_start(const std::vector<Token>& tokens, std::size_t index) {
    index = specifiers_start(tokens, index);
    // Skip back over a template header before applying the usual "previous
    // token ends a declaration/statement/label" rule.
    if (const std::optional<std::size_t> header = template_header_start(tokens, index); header.has_value()) {
        index = *header;
    }
    if (index == 0) {
        return true;
    }
    const Token& previous = tokens[index - 1];
    return previous.is_punctuator(";") || previous.is_punctuator("{") || previous.is_punctuator("}") ||
           previous.is_punctuator(":");
}

std::size_t matching_parenthesis(const std::vector<Token>& tokens, std::size_t open) {
    std::size_t depth = 0;
    for (std::size_t index = open; index < tokens.size(); ++index) {
        if (tokens[index].is_punctuator("(")) {
            ++depth;
        } else if (tokens[index].is_punctuator(")")) {
            --depth;
            if (depth == 0) {
                return index;
            }
        } else if (tokens[index].kind == TokenKind::EndOfFile) {
            break;
        }
    }
    return tokens.size();
}

// The `>` closing the argument list opened at `open`.
//
// Angle brackets are not self-delimiting in C++, so this stops at a token that
// cannot appear inside an argument list rather than scanning to end of file.
// It is only ever used to find where a declarator's parameter list begins;
// which specialization the arguments denote is Clang's to resolve.
std::size_t matching_angle_bracket(const std::vector<Token>& tokens, std::size_t open) {
    std::size_t depth = 0;
    for (std::size_t index = open; index < tokens.size(); ++index) {
        if (tokens[index].is_punctuator("<")) {
            ++depth;
        } else if (tokens[index].is_punctuator(">")) {
            --depth;
            if (depth == 0) {
                return index;
            }
        } else if (tokens[index].is_punctuator(";") || tokens[index].is_punctuator("{") ||
                   tokens[index].kind == TokenKind::EndOfFile) {
            break;
        }
    }
    return tokens.size();
}

std::size_t matching_bracket(const std::vector<Token>& tokens, std::size_t open) {
    std::size_t depth = 0;
    for (std::size_t index = open; index < tokens.size(); ++index) {
        if (tokens[index].is_punctuator("[")) {
            ++depth;
        } else if (tokens[index].is_punctuator("]")) {
            --depth;
            if (depth == 0) {
                return index;
            }
        } else if (tokens[index].kind == TokenKind::EndOfFile) {
            break;
        }
    }
    return tokens.size();
}

std::size_t matching_brace(const std::vector<Token>& tokens, std::size_t open) {
    std::size_t depth = 0;
    for (std::size_t index = open; index < tokens.size(); ++index) {
        if (tokens[index].is_punctuator("{")) {
            ++depth;
        } else if (tokens[index].is_punctuator("}")) {
            --depth;
            if (depth == 0) {
                return index;
            }
        } else if (tokens[index].kind == TokenKind::EndOfFile) {
            break;
        }
    }
    return tokens.size();
}

std::optional<ClauseKind> clause_kind(const Token& token) {
    if (token.is_identifier("decreases")) {
        return ClauseKind::Decreases;
    }
    if (token.is_identifier("proves")) {
        return ClauseKind::Proves;
    }
    if (token.is_identifier("ensures")) {
        return ClauseKind::Ensures;
    }
    if (token.is_identifier("expects")) {
        return ClauseKind::Expects;
    }
    return std::nullopt;
}

bool is_specification_clause(const Token& token) {
    return clause_kind(token).has_value() || token.is_identifier("decreases");
}

void report(diagnostics::Engine& engine, const source::SourceLocation& location, diagnostics::Category category,
            std::string message, std::string note = {}) {
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = diagnostics::Severity::Error;
    diagnostic.category = category;
    diagnostic.message = std::move(message);
    diagnostic.location = location;
    if (!note.empty()) {
        diagnostic.notes.push_back(diagnostics::Note{std::move(note), diagnostic.location});
    }
    engine.report(std::move(diagnostic));
}

void report(diagnostics::Engine& engine, const TokenStream& stream, const Token& token, diagnostics::Category category,
            std::string message, std::string note = {}) {
    report(engine, stream.location_of(token), category, std::move(message), std::move(note));
}

// Why a clause of `kind` may not follow the clauses `seen`, or nothing when it
// may: each kind is written once, and `expects` precedes the conclusion.
// `check_clause_sequence` refuses what this names, and `clauses_admitted`
// offers only what it does not.
std::optional<std::string> out_of_sequence(ClauseKind kind, const std::vector<ClauseKind>& seen, bool conclusion) {
    if (std::ranges::find(seen, kind) != seen.end()) {
        return "use one '" + describe(kind) + "' clause; combine conjoined predicates with '&&'";
    }
    if (kind == ClauseKind::Expects && conclusion) {
        return "'expects' must precede the conclusion clause";
    }
    return std::nullopt;
}

void check_clause_sequence(const std::vector<Clause>& clauses, diagnostics::Engine& engine) {
    std::vector<ClauseKind> seen;
    bool conclusion = false;
    for (const auto& clause : clauses) {
        if (std::optional<std::string> message = out_of_sequence(clause.kind, seen, conclusion)) {
            diagnostics::Diagnostic diagnostic;
            diagnostic.severity = diagnostics::Severity::Error;
            diagnostic.category = diagnostics::Category::CpplSyntax;
            diagnostic.location = clause.location;
            diagnostic.message = std::move(*message);
            engine.report(std::move(diagnostic));
        }
        seen.push_back(clause.kind);
        conclusion = conclusion || clause.kind == ClauseKind::Ensures || clause.kind == ClauseKind::Proves;
    }
}

// `law` introduces a Law only when a contract clause follows the parameter
// list. Up to that point the token sequence is still ordinary C++ (a function
// returning a type named `law`, for instance), so nothing is reinterpreted
// until the clause makes the ordinary C++ reading impossible (SPEC.md 3.1).
bool read_proof_statements(const TokenStream& stream, std::size_t body_open, std::size_t body_close,
                           diagnostics::Engine& engine, std::vector<ProofStatement>& statements, unsigned nesting = 0,
                           bool draft = false);

// Reads the statements of the block from `body_open` to `body_close`, as
// `read_proof_statements` does. In a draft, a statement it cannot read is kept
// as `Unread` -- up to the `;` that ends it, or the `}` closing a block it
// opens -- and reading resumes after it, so the block is always read to its end.
bool read_block(const TokenStream& stream, std::size_t body_open, std::size_t body_close, diagnostics::Engine& engine,
                std::vector<ProofStatement>& statements, unsigned nesting, bool draft);

// A span from the first byte of `first` through the last byte of `last`.
source::ByteSpan tokens_span(const Token& first, const Token& last) {
    return source::ByteSpan{first.span.offset, last.span.end() - first.span.offset};
}

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

// Splits the instantiation arguments of `exact p(a, b)` at the commas that
// separate them.
//
// Only the separators are found here. Each argument is delimited, never read:
// its bytes go to Clang through the projection, which is what keeps C++
// expression meaning in one place (SPEC.md 7.3, ARCHITECTURE.md 7).
bool read_proof_arguments(const TokenStream& stream, std::size_t open, std::size_t close, diagnostics::Engine& engine,
                          std::vector<ProofArgument>& arguments) {
    const std::vector<Token>& tokens = stream.tokens();

    std::size_t depth = 0;
    std::size_t begin = open + 1;
    for (std::size_t index = open + 1; index <= close; ++index) {
        const Token& token = tokens[index];
        const bool separator = depth == 0 && (index == close || token.is_punctuator(","));

        if (!separator) {
            if (token.is_punctuator("(") || token.is_punctuator("[") || token.is_punctuator("{")) {
                ++depth;
            } else if (token.is_punctuator(")") || token.is_punctuator("]") || token.is_punctuator("}")) {
                if (depth == 0) {
                    report(engine, stream, token, diagnostics::Category::CpplSyntax,
                           "'" + std::string(token.text) +
                               "' closes nothing in this "
                               "instantiation argument list");
                    return false;
                }
                --depth;
            }
            continue;
        }

        if (index == begin) {
            if (index == close && arguments.empty()) {
                return true; // `p()` instantiates at nothing, like `p`
            }
            report(engine, stream, token, diagnostics::Category::CpplSyntax, "an instantiation argument is missing",
                   "each argument of a proof reference is an ordinary C++ expression");
            return false;
        }

        arguments.push_back(ProofArgument{
            source::ByteSpan{tokens[begin].span.offset, tokens[index - 1].span.end() - tokens[begin].span.offset},
            stream.location_of(tokens[begin])});
        begin = index + 1;
    }

    return true;
}

// Reads the primitive proof statements of GRAMMAR.md 5 out of a proof body.
//
// A proof statement names proof-level entities only. No C++ expression is read
// here: the proposition a proof discharges is resolved by Clang from the
// projected text, never by this recognizer.
bool read_proof_statements(const TokenStream& stream, std::size_t body_open, std::size_t body_close,
                           diagnostics::Engine& engine, std::vector<ProofStatement>& statements, unsigned nesting,
                           bool draft) {
    const std::vector<Token>& tokens = stream.tokens();
    if (nesting > 32) {
        report(engine, stream, tokens[body_open], diagnostics::Category::CpplSyntax,
               "proof arms nest deeper than the supported limit");
        return false;
    }

    std::size_t cursor = body_open + 1;
    while (cursor < body_close) {
        const Token& token = tokens[cursor];

        if (token.is_identifier("cases") || token.is_identifier("decompose")) {
            std::size_t open = cursor + 1;
            unsigned parens = 0, brackets = 0;
            for (; open < body_close; ++open) {
                if (tokens[open].is_punctuator("("))
                    ++parens;
                if (tokens[open].is_punctuator(")") && parens != 0)
                    --parens;
                if (tokens[open].is_punctuator("["))
                    ++brackets;
                if (tokens[open].is_punctuator("]") && brackets != 0)
                    --brackets;
                if (parens == 0 && brackets == 0 && tokens[open].is_punctuator("{"))
                    break;
                if (tokens[open].is_punctuator(";"))
                    break;
            }
            if (open >= body_close || open == cursor + 1 || !tokens[open].is_punctuator("{")) {
                report(engine, stream, token, diagnostics::Category::CpplSyntax,
                       "decomposition requires a subject and a body");
                return false;
            }
            ProofStatement statement;
            statement.kind = token.is_identifier("cases") ? ProofStatementKind::Cases : ProofStatementKind::Decompose;
            statement.proposition = {tokens[cursor + 1].span.offset,
                                     tokens[open - 1].span.end() - tokens[cursor + 1].span.offset};
            statement.reference = std::string(stream.spelling(statement.proposition));
            statement.keyword = token.span;
            statement.location = stream.location_of(token);
            const std::size_t end = matching_brace(tokens, open);
            if (end >= body_close) {
                report(engine, stream, token, diagnostics::Category::CpplSyntax,
                       "unterminated decomposition statement");
                return false;
            }
            statement.arms_span = {tokens[open].span.offset, tokens[end].span.end() - tokens[open].span.offset};
            cursor = open + 1;
            // Where a case label starting at `from` ends: the index just past
            // it, or `from` itself when no label starts there. A label is an
            // id-expression (GRAMMAR.md 5.9), so any of its parts may carry
            // template arguments, as `Machine<int>::Mode::on` and
            // `alternative<0>` do.
            const auto label_end = [&](std::size_t from) {
                std::size_t at = from;
                if (at < end && tokens[at].is_punctuator("::"))
                    ++at;
                if (at >= end || tokens[at].kind != TokenKind::Identifier)
                    return from;
                while (true) {
                    ++at;
                    if (at < end && tokens[at].is_punctuator("<")) {
                        unsigned angles = 0;
                        do {
                            if (tokens[at].is_punctuator("<"))
                                ++angles;
                            if (tokens[at].is_punctuator(">"))
                                --angles;
                            if (tokens[at].is_punctuator(">>"))
                                angles = angles >= 2 ? angles - 2 : 0;
                            ++at;
                        } while (at < end && angles != 0);
                    }
                    if (at + 1 >= end || !tokens[at].is_punctuator("::") ||
                        tokens[at + 1].kind != TokenKind::Identifier)
                        return at;
                    ++at;
                }
            };
            bool malformed = false;
            while (cursor < end) {
                malformed = true;
                ProofArm arm;
                arm.location = stream.location_of(tokens[cursor]);
                const std::size_t omit_start = cursor;
                // `omit label by contradiction evidence;` (GRAMMAR.md 5.7).
                // `omit` means this only where that whole form follows, so it
                // stays an ordinary name everywhere else, including as the first
                // part of a label such as `omit::State::idle` (SPEC.md
                // WORD-010). Only `cases` has omissions: a product has one
                // state, so `decompose` has nothing to omit.
                const std::size_t omitted_label_end = label_end(cursor + 1);
                const bool omitting = statement.kind == ProofStatementKind::Cases &&
                                      tokens[cursor].is_identifier("omit") && omitted_label_end > cursor + 1 &&
                                      omitted_label_end < end && tokens[omitted_label_end].is_identifier("by");
                if (omitting) {
                    arm.omitted = true;
                    ++cursor;
                }
                const std::size_t start = cursor;
                cursor = label_end(start);
                if (cursor == start)
                    break;
                arm.label = {tokens[start].span.offset, tokens[cursor - 1].span.end() - tokens[start].span.offset};
                arm.spelling = std::string(stream.spelling(arm.label));
                // Which kind of label this is belongs to the representation, not
                // to the syntax. The parser only separates a name a
                // representation reserves for a state with no C++ expression
                // from a label Clang is to resolve; which case either denotes is
                // settled later, by the provider (SPEC.md 20.1).
                arm.keyword_label = decomposition::label_kind(arm.spelling) == decomposition::LabelKind::Keyword;
                if (tokens[start].is_identifier("_")) {
                    report(engine, stream, tokens[start], diagnostics::Category::CpplSyntax,
                           "cases has no wildcard arm");
                    return false;
                }
                // An unqualified name is a reserved label or nothing. Requiring
                // qualification everywhere else is what keeps a reserved label
                // distinct from an enumerator that happens to share its
                // spelling, such as `unnamed` in `enum class State { unnamed }`.
                if (!arm.keyword_label && cursor == start + 1) {
                    report(engine, stream, tokens[start], diagnostics::Category::UnsupportedSemantics,
                           "a case label must be qualified, as in 'State::idle', unless it is a "
                           "name the representation reserves");
                    return false;
                }
                if (omitting) {
                    // No binders: an omitted case has no body to bind them in.
                    // What follows `by` is an ordinary `contradiction` statement,
                    // evidence reference and arguments included, so it is read by
                    // the parser every other proof statement goes through.
                    const std::size_t by = cursor;
                    std::size_t terminator = by + 1;
                    int depth = 0;
                    for (; terminator < end; ++terminator) {
                        const Token& at = tokens[terminator];
                        if (depth == 0 && at.is_punctuator(";"))
                            break;
                        if (at.is_punctuator("(") || at.is_punctuator("[") || at.is_punctuator("{"))
                            ++depth;
                        else if (at.is_punctuator(")") || at.is_punctuator("]") || at.is_punctuator("}"))
                            --depth;
                    }
                    const auto malformed_omission = [&] {
                        report(engine, stream, tokens[omit_start], diagnostics::Category::CpplSyntax,
                               "an omitted case is written 'omit label by contradiction evidence;'");
                        return false;
                    };
                    if (terminator >= end || by + 1 >= terminator || !tokens[by + 1].is_identifier("contradiction"))
                        return malformed_omission();
                    if (!read_proof_statements(stream, by, terminator + 1, engine, arm.statements, nesting + 1))
                        return false;
                    if (arm.statements.size() != 1 || arm.statements[0].kind != ProofStatementKind::Contradiction)
                        return malformed_omission();
                    arm.discharge_span = {tokens[by + 1].span.offset,
                                          tokens[terminator].span.end() - tokens[by + 1].span.offset};
                    arm.span = {tokens[omit_start].span.offset,
                                tokens[terminator].span.end() - tokens[omit_start].span.offset};
                    statement.arms.push_back(std::move(arm));
                    malformed = false;
                    cursor = terminator + 1;
                    continue;
                }
                if (cursor < end && tokens[cursor].is_punctuator("(")) {
                    ++cursor;
                    if (cursor >= end || tokens[cursor].kind != TokenKind::Identifier)
                        break;
                    while (cursor < end && tokens[cursor].kind == TokenKind::Identifier) {
                        arm.binders.emplace_back(tokens[cursor++].text);
                        if (cursor >= end || !tokens[cursor].is_punctuator(","))
                            break;
                        ++cursor;
                        if (cursor >= end || tokens[cursor].kind != TokenKind::Identifier) {
                            report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                                   "a case binder list requires an identifier after ','");
                            return false;
                        }
                    }
                    if (cursor >= end || !tokens[cursor].is_punctuator(")"))
                        break;
                    ++cursor;
                }
                // The C++ token stream spells the proof arrow as '=' followed by '>'.
                if (cursor + 2 >= end || !tokens[cursor].is_punctuator("=") || !tokens[cursor + 1].is_punctuator(">") ||
                    !tokens[cursor + 2].is_punctuator("{"))
                    break;
                cursor += 2;
                const std::size_t close = matching_brace(tokens, cursor);
                if (close >= end || !read_block(stream, cursor, close, engine, arm.statements, nesting + 1, draft))
                    return false;
                arm.body_span = {tokens[cursor].span.offset, tokens[close].span.end() - tokens[cursor].span.offset};
                arm.span = {tokens[start].span.offset, tokens[close].span.end() - tokens[start].span.offset};
                statement.arms.push_back(std::move(arm));
                malformed = false;
                cursor = close + 1;
            }
            if (malformed || cursor != end || statement.arms.empty() || statement.arms.size() > 64) {
                report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                       "cases requires 1 to 64 arms of the form 'label(binders) => { proof statements }' "
                       "or omissions of the form 'omit label by contradiction evidence;'");
                return false;
            }
            statement.span = tokens_span(token, tokens[end]);
            statements.push_back(std::move(statement));
            cursor = end + 1;
            continue;
        }

        // induction identifier ";"                              (short form)
        // induction identifier "{" proof-arm {proof-arm} "}"    (GRAMMAR.md 5.8)
        //
        // Recognized the same way `cases`/`decompose` are, syntax only: which
        // labels a domain's induction principle actually admits (`zero`,
        // `successor(pred)`, ...) is not the decomposition-provider label
        // vocabulary (`cppl::decomposition::label_kind` - that is for the
        // subject a `cases`/`decompose` state partition is defined over, an
        // unrelated concept), so an induction arm label is read here as a
        // plain, unqualified identifier, optionally with a binder list. This
        // implementation's formal core has no induction rule (SPEC.md 21),
        // so which labels/arities are actually legal is left entirely to the
        // semantic layer, which continues to reject every induction proof
        // outright (elaborate.cpp) - recognizing the syntax here only lets
        // the FORMATTER lay out what was written.
        if (token.is_identifier("induction") && cursor + 1 < body_close &&
            tokens[cursor + 1].kind == TokenKind::Identifier) {
            const std::size_t subject = cursor + 1;
            ProofStatement statement;
            statement.kind = ProofStatementKind::Induction;
            statement.proposition = tokens[subject].span;
            statement.reference = std::string(tokens[subject].text);
            statement.keyword = token.span;
            statement.location = stream.location_of(token);

            if (subject + 1 < body_close && tokens[subject + 1].is_punctuator(";")) {
                // Short form: automation is requested for every case: no arms
                // to recognize.
                statement.span = tokens_span(token, tokens[subject + 1]);
                statements.push_back(std::move(statement));
                cursor = subject + 2;
                continue;
            }
            if (subject + 1 >= body_close || !tokens[subject + 1].is_punctuator("{")) {
                report(engine, stream, token, diagnostics::Category::CpplSyntax,
                       "'induction' names a subject, then ';' or '{ arms }'");
                return false;
            }
            const std::size_t open = subject + 1;
            const std::size_t end = matching_brace(tokens, open);
            if (end >= body_close) {
                report(engine, stream, token, diagnostics::Category::CpplSyntax, "unterminated induction statement");
                return false;
            }
            statement.arms_span = {tokens[open].span.offset, tokens[end].span.end() - tokens[open].span.offset};
            cursor = open + 1;
            bool malformed = false;
            while (cursor < end) {
                malformed = true;
                ProofArm arm;
                arm.location = stream.location_of(tokens[cursor]);
                const std::size_t start = cursor;
                if (tokens[cursor].kind != TokenKind::Identifier)
                    break;
                ++cursor;
                arm.label = {tokens[start].span.offset, tokens[start].span.end() - tokens[start].span.offset};
                arm.spelling = std::string(stream.spelling(arm.label));
                arm.keyword_label = true; // an induction label is never a C++ expression Clang resolves
                if (cursor < end && tokens[cursor].is_punctuator("(")) {
                    ++cursor;
                    if (cursor >= end || tokens[cursor].kind != TokenKind::Identifier)
                        break;
                    while (cursor < end && tokens[cursor].kind == TokenKind::Identifier) {
                        arm.binders.emplace_back(tokens[cursor++].text);
                        if (cursor >= end || !tokens[cursor].is_punctuator(","))
                            break;
                        ++cursor;
                        if (cursor >= end || tokens[cursor].kind != TokenKind::Identifier) {
                            report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                                   "an induction binder list requires an identifier after ','");
                            return false;
                        }
                    }
                    if (cursor >= end || !tokens[cursor].is_punctuator(")"))
                        break;
                    ++cursor;
                }
                // The C++ token stream spells the proof arrow as '=' followed by '>'.
                if (cursor + 2 >= end || !tokens[cursor].is_punctuator("=") || !tokens[cursor + 1].is_punctuator(">") ||
                    !tokens[cursor + 2].is_punctuator("{"))
                    break;
                cursor += 2;
                const std::size_t close = matching_brace(tokens, cursor);
                if (close >= end || !read_block(stream, cursor, close, engine, arm.statements, nesting + 1, draft))
                    return false;
                arm.body_span = {tokens[cursor].span.offset, tokens[close].span.end() - tokens[cursor].span.offset};
                arm.span = {tokens[start].span.offset, tokens[close].span.end() - tokens[start].span.offset};
                statement.arms.push_back(std::move(arm));
                malformed = false;
                cursor = close + 1;
            }
            if (malformed || cursor != end || statement.arms.empty() || statement.arms.size() > 64) {
                report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                       "induction requires 1 to 64 arms of the form 'label(binders) => { proof statements }'");
                return false;
            }
            statement.span = tokens_span(token, tokens[end]);
            statements.push_back(std::move(statement));
            cursor = end + 1;
            continue;
        }

        if (token.is_identifier("refl") && cursor + 1 < body_close && tokens[cursor + 1].is_punctuator(";")) {
            ProofStatement statement;
            statement.kind = ProofStatementKind::Reflexivity;
            statement.keyword = token.span;
            statement.location = stream.location_of(token);
            statement.span = tokens_span(token, tokens[cursor + 1]);
            statements.push_back(std::move(statement));
            cursor += 2;
            continue;
        }

        // `assume h : P;` names a premise the goal already supposes. The
        // proposition is delimited here and read by Clang, like every other
        // expression a proof statement carries.
        if (token.is_identifier("assume") && cursor + 3 < body_close &&
            tokens[cursor + 1].kind == TokenKind::Identifier && tokens[cursor + 2].is_punctuator(":")) {
            std::size_t depth = 0;
            std::size_t terminator = cursor + 3;
            while (terminator < body_close) {
                const Token& candidate = tokens[terminator];
                if (candidate.is_punctuator("(") || candidate.is_punctuator("[")) {
                    ++depth;
                } else if (candidate.is_punctuator(")") || candidate.is_punctuator("]")) {
                    if (depth == 0) {
                        break;
                    }
                    --depth;
                } else if (depth == 0 && candidate.is_punctuator(";")) {
                    break;
                }
                ++terminator;
            }

            if (terminator >= body_close || !tokens[terminator].is_punctuator(";") || terminator == cursor + 3) {
                report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                       "'assume' names a proposition, as in 'assume h : a == b;'");
                return false;
            }

            ProofStatement statement;
            statement.kind = ProofStatementKind::Assume;
            statement.reference = std::string(tokens[cursor + 1].text);
            statement.reference_location = stream.location_of(tokens[cursor + 1]);
            statement.proposition = source::ByteSpan{
                tokens[cursor + 3].span.offset, tokens[terminator - 1].span.end() - tokens[cursor + 3].span.offset};
            statement.proposition_location = stream.location_of(tokens[cursor + 3]);
            statement.keyword = token.span;
            statement.location = stream.location_of(token);
            statement.span = tokens_span(token, tokens[terminator]);
            statements.push_back(std::move(statement));
            cursor = terminator + 1;
            continue;
        }

        // Like every proof-statement word, `contradiction` means this only here,
        // at the start of a statement in a proof body (SPEC.md WORD-002).
        const std::optional<ProofStatementKind> named =
            token.kind == TokenKind::Identifier ? statement_keyword(token.text) : std::nullopt;
        if (named.has_value() && names_evidence(*named) && cursor + 2 < body_close &&
            tokens[cursor + 1].kind == TokenKind::Identifier) {
            ProofStatement statement;
            statement.kind = *named;
            statement.reference = std::string(tokens[cursor + 1].text);
            statement.reference_location = stream.location_of(tokens[cursor + 1]);
            statement.keyword = token.span;
            statement.location = stream.location_of(token);

            if (tokens[cursor + 2].is_punctuator(";")) {
                statement.span = tokens_span(token, tokens[cursor + 2]);
                statements.push_back(std::move(statement));
                cursor += 3;
                continue;
            }

            if (tokens[cursor + 2].is_punctuator("(")) {
                const std::size_t close = matching_parenthesis(tokens, cursor + 2);
                if (close >= body_close) {
                    report(engine, stream, tokens[cursor + 2], diagnostics::Category::CpplSyntax,
                           "unterminated instantiation argument list");
                    return false;
                }
                if (!read_proof_arguments(stream, cursor + 2, close, engine, statement.arguments)) {
                    return false;
                }
                if (close + 1 >= body_close || !tokens[close + 1].is_punctuator(";")) {
                    report(engine, stream, tokens[close], diagnostics::Category::CpplSyntax,
                           "a proof statement ends with ';'");
                    return false;
                }
                statement.span = tokens_span(token, tokens[close + 1]);
                statements.push_back(std::move(statement));
                cursor = close + 2;
                continue;
            }
        }

        report(engine, stream, token, diagnostics::Category::UnsupportedSemantics,
               "'" + std::string(token.text) +
                   "' does not begin a proof statement this "
                   "implementation supports",
               "the supported proof statements are 'refl;', 'exact <evidence>;', "
               "'apply <evidence>;', 'rewrite <evidence>;', 'contradiction <evidence>;', "
               "'assume <name> : <proposition>;', and 'cases <subject> { ... }' and "
               "'decompose <subject> { ... }' with an arm for each state. Evidence may be "
               "instantiated at arguments, as in 'exact <proof>(<expression>);'");
        return false;
    }

    return true;
}

bool read_block(const TokenStream& stream, std::size_t body_open, std::size_t body_close, diagnostics::Engine& engine,
                std::vector<ProofStatement>& statements, unsigned nesting, bool draft) {
    if (!draft) {
        return read_proof_statements(stream, body_open, body_close, engine, statements, nesting);
    }
    const std::vector<Token>& tokens = stream.tokens();
    std::size_t from = body_open;
    while (from + 1 < body_close) {
        const std::size_t kept = statements.size();
        if (read_proof_statements(stream, from, body_close, engine, statements, nesting, true)) {
            return true;
        }
        // The reader stops at the first statement it cannot read, keeping
        // every statement before it; that statement starts after the last one.
        std::size_t start = from + 1;
        if (statements.size() > kept) {
            const std::size_t read_to = statements.back().span.end();
            while (start < body_close && tokens[start].span.offset < read_to) {
                ++start;
            }
        }
        if (start >= body_close) {
            return true;
        }
        std::size_t last = start;
        std::size_t depth = 0;
        for (std::size_t at = start; at < body_close; ++at) {
            last = at;
            const Token& token = tokens[at];
            if (token.is_punctuator("(") || token.is_punctuator("[") || token.is_punctuator("{")) {
                ++depth;
            } else if (token.is_punctuator(")") || token.is_punctuator("]") || token.is_punctuator("}")) {
                if (depth > 0) {
                    --depth;
                    if (depth == 0 && token.is_punctuator("}")) {
                        break;
                    }
                }
            } else if (depth == 0 && token.is_punctuator(";")) {
                break;
            }
        }
        ProofStatement unread;
        unread.kind = ProofStatementKind::Unread;
        unread.keyword = tokens[start].span;
        unread.location = stream.location_of(tokens[start]);
        unread.span = tokens_span(tokens[start], tokens[last]);
        statements.push_back(std::move(unread));
        from = last;
    }
    return true;
}

// `proof` introduces a proof declaration only when `proves` follows the
// parameter list. Until then the token sequence is still ordinary C++ - a
// function returning a type named `proof`, for instance (SPEC.md 3.1).
bool try_proof(const TokenStream& stream, std::size_t index, diagnostics::Engine& engine, ProofDeclaration& proof,
               std::size_t& next_index, RecognitionMode mode) {
    const std::vector<Token>& tokens = stream.tokens();

    if (index + 2 >= tokens.size()) {
        return false;
    }
    if (tokens[index + 1].kind != TokenKind::Identifier || !tokens[index + 2].is_punctuator("(")) {
        return false;
    }

    const bool draft = mode == RecognitionMode::Draft;
    const std::size_t close = matching_parenthesis(tokens, index + 2);
    const auto head = [&] {
        proof.name = std::string(tokens[index + 1].text);
        proof.name_location = stream.location_of(tokens[index + 1]);
        proof.name_span = tokens[index + 1].span;
        proof.range.begin = proof.name_location;
        proof.keyword_location = stream.location_of(tokens[index]);
        proof.parameters =
            source::ByteSpan{tokens[index + 2].span.end(), tokens[close].span.offset - tokens[index + 2].span.end()};
    };
    if (close >= tokens.size() || close + 1 >= tokens.size() || !tokens[close + 1].is_identifier("proves")) {
        // Still ordinary C++. A draft keeps where `proves` would make it a
        // proof.
        if (draft && close < tokens.size()) {
            head();
            proof.range.span = tokens_span(tokens[index], tokens[close]);
            proof.completeness = Completeness::AwaitingClause;
        }
        return false;
    }

    const std::size_t proves = close + 1;
    if (proves + 1 >= tokens.size() || !tokens[proves + 1].is_punctuator("(")) {
        report(engine, stream, tokens[proves], diagnostics::Category::CpplSyntax,
               "'proves' must be followed by a parenthesized specification expression");
        next_index = proves + 1;
        return true;
    }

    const std::size_t proves_close = matching_parenthesis(tokens, proves + 1);
    if (proves_close >= tokens.size()) {
        report(engine, stream, tokens[proves + 1], diagnostics::Category::CpplSyntax,
               "unterminated specification expression");
        next_index = proves + 2;
        return true;
    }
    const auto claim = [&] {
        proof.proves_keyword = tokens[proves].span;
        proof.proposition = source::ByteSpan{tokens[proves + 1].span.end(),
                                             tokens[proves_close].span.offset - tokens[proves + 1].span.end()};
        proof.proposition_location = stream.location_of(tokens[proves]);
    };

    if (proves_close + 1 >= tokens.size() || !tokens[proves_close + 1].is_punctuator("{")) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax, "a proof declaration has a body",
               "a proof constructs evidence, so it ends with '{ ... }' rather than ';'");
        if (draft) {
            head();
            claim();
            proof.range.span = tokens_span(tokens[index], tokens[proves_close]);
            proof.completeness = Completeness::AwaitingBody;
        }
        next_index = proves_close + 1;
        return true;
    }

    const std::size_t body_open = proves_close + 1;
    const std::size_t body_close = matching_brace(tokens, body_open);
    if (body_close >= tokens.size()) {
        report(engine, stream, tokens[body_open], diagnostics::Category::CpplSyntax, "unterminated proof body");
        if (draft) {
            // The body runs to the end of the text, and what is written
            // after the `{` is read on as well.
            head();
            claim();
            const std::size_t end = tokens.size() - 1;
            proof.range.span = tokens_span(tokens[index], tokens[end]);
            proof.body = tokens_span(tokens[body_open], tokens[end]);
            proof.completeness = Completeness::UnterminatedBody;
            read_block(stream, body_open, end, engine, proof.statements, 0, true);
        }
        next_index = body_open + 1;
        return true;
    }

    next_index = body_close + 1;

    head();
    claim();
    proof.range.span =
        source::ByteSpan{tokens[index].span.offset, tokens[body_close].span.end() - tokens[index].span.offset};
    proof.body = tokens_span(tokens[body_open], tokens[body_close]);
    proof.end_line = tokens[body_close].line;

    bool malformed = stream.spelling(proof.proposition).find_first_not_of(" \t\r\n") == std::string_view::npos;
    if (malformed) {
        report(engine, stream, tokens[proves], diagnostics::Category::CpplSyntax, "'proves' requires a proposition");
    }

    if (!read_block(stream, body_open, body_close, engine, proof.statements, 0, draft)) {
        malformed = true;
    } else if (proof.statements.empty()) {
        report(engine, stream, tokens[index], diagnostics::Category::ProofFailure,
               "proof '" + proof.name + "' has an empty body", "a proof body must close the goal it states");
        malformed = true;
    }

    if (malformed && mode == RecognitionMode::Compile) {
        proof.name.clear();
        proof.statements.clear();
    }
    return true;
}

// `pure` and `verified` are declaration specifiers only where the following
// tokens cannot begin an ordinary declaration whose type carries that name.
bool specifier_introduces_declaration(const std::vector<Token>& tokens, std::size_t index) {
    if (index + 1 >= tokens.size()) {
        return false;
    }
    const Token& next = tokens[index + 1];
    if (is_type_keyword(next) || next.is_punctuator("::") || next.is_identifier("pure")) {
        return true;
    }
    if (next.kind != TokenKind::Identifier || index + 2 >= tokens.size()) {
        return false;
    }
    const Token& after = tokens[index + 2];
    return after.kind == TokenKind::Identifier || after.is_punctuator("::") || after.is_punctuator("<") ||
           after.is_punctuator("*") || after.is_punctuator("&") || after.is_punctuator("&&");
}

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

std::optional<std::size_t> find_declarator_name(const std::vector<Token>& tokens, std::size_t index) {
    std::size_t depth = 0;
    for (std::size_t cursor = index + 1; cursor < tokens.size(); ++cursor) {
        const Token& token = tokens[cursor];
        if (token.kind == TokenKind::EndOfFile) {
            break;
        }
        if (token.is_punctuator("(")) {
            if (depth == 0 && cursor > index + 1 && tokens[cursor - 1].kind == TokenKind::Identifier &&
                !is_type_keyword(tokens[cursor - 1])) {
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
    instantiation.function_name = std::string(tokens[*name].text);
    instantiation.location = stream.location_of(tokens[*name]);
    instantiation.id_expression =
        source::ByteSpan{tokens[start].span.offset, tokens[parameters].span.offset - tokens[start].span.offset};
    instantiation.insertion_offset = tokens[end].span.end();
    instantiation.insertion_line = tokens[end].line;
    next_index = end + 1;
    return true;
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
std::size_t skip_ordinary_declarator_suffix(const std::vector<Token>& tokens, std::size_t cursor) {
    std::size_t nesting = 0;
    while (cursor < tokens.size()) {
        const Token& token = tokens[cursor];
        if (token.kind == TokenKind::EndOfFile || token.is_punctuator("{") || token.is_punctuator(";")) {
            break;
        }
        if (nesting == 0 && token.is_punctuator(",")) {
            break;
        }
        if (nesting == 0 && is_specification_clause(token) && cursor + 1 < tokens.size() &&
            tokens[cursor + 1].is_punctuator("(")) {
            break;
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

// Specification clauses on a function declarator are part of the language
// (GRAMMAR.md 6) but are not verified by this implementation. They are
// diagnosed rather than erased, because silently dropping a contract would
// turn a specification into nothing at all.
//
// A clause stands only where the declarator's ordinary suffix ends, so only
// that one position is examined.
bool has_specification_clause(const std::vector<Token>& tokens, std::size_t name_index, std::size_t& clause_index) {
    const std::size_t open = name_index + 1;
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
                                                 diagnostics::Engine& engine, std::vector<Clause>& clauses,
                                                 bool report_decreases_unsupported) {
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
        if (*kind == ClauseKind::Decreases && report_decreases_unsupported) {
            report(engine, stream, tokens[cursor], diagnostics::Category::UnsupportedSemantics,
                   "function termination is not verified by this implementation",
                   "the requested 'decreases' obligation must not be accepted unchecked");
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
        if (tokens[cursor].is_identifier("verified") || tokens[cursor].is_identifier("pure") ||
            tokens[cursor].is_identifier("law") || tokens[cursor].is_identifier("proof") ||
            tokens[cursor].is_identifier("trusted")) {
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
    const std::size_t open = name_index + 1;
    const std::size_t close = matching_parenthesis(tokens, open);
    if (close >= tokens.size()) {
        return false;
    }
    const std::size_t first_clause = skip_ordinary_declarator_suffix(tokens, close + 1);
    VerifiedFunction layout;
    const std::optional<std::size_t> scanned =
        scan_function_clauses(stream, first_clause, engine, layout.clauses, /*report_decreases_unsupported=*/false);
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

// `verified` marks a function whose contract this implementation has to
// discharge. The clauses are delimited here; what they mean is settled once
// Clang has resolved them, like every other specification expression.
bool try_verified(const TokenStream& stream, std::size_t index, diagnostics::Engine& engine, VerifiedFunction& verified,
                  std::size_t& next_index) {
    const std::vector<Token>& tokens = stream.tokens();
    next_index = index + 1;

    const std::optional<std::size_t> name = find_declarator_name(tokens, index);
    if (!name.has_value()) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "the 'verified' specifier applies to a function declaration",
               "no function declarator follows this specifier");
        return false;
    }

    // An explicit specialization's declarator is `name<args>(...)`, so the
    // parameter list opens after the arguments rather than after the name.
    std::size_t open = *name + 1;
    if (open < tokens.size() && tokens[open].is_punctuator("<")) {
        const std::size_t arguments_close = matching_angle_bracket(tokens, open);
        if (arguments_close >= tokens.size()) {
            return false;
        }
        open = arguments_close + 1;
    }
    if (open >= tokens.size() || !tokens[open].is_punctuator("(")) {
        return false;
    }
    const std::size_t close = matching_parenthesis(tokens, open);
    if (close >= tokens.size()) {
        return false;
    }

    // Whatever stands between the specifiers and the declarator is the return
    // type, and the contract's `result` is a value of it.
    std::size_t type_start = index + 1;
    const bool also_pure = tokens[type_start].is_identifier("pure");
    if (also_pure) {
        ++type_start;
    }
    if (type_start >= *name) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "a verified function states a return type before its name");
        return false;
    }
    // `auto` naming a trailing return type (`auto f(...) -> T`) still states
    // the return type explicitly, just after the parameter list rather than
    // before the name (GRAMMAR.md 43); only a genuinely deduced return type
    // (no `->` at all) leaves `result`'s type unwritten and unsupported.
    const bool trailing_return = close + 1 < tokens.size() && tokens[close + 1].is_punctuator("->");
    if (tokens[type_start].is_identifier("auto") && !trailing_return) {
        report(engine, stream, tokens[type_start], diagnostics::Category::UnsupportedSemantics,
               "a deduced return type is not supported on a verified function",
               "the contract's 'result' is a value of the declared return type, so this "
               "implementation requires one to be written");
        return false;
    }

    const std::size_t first_clause = skip_ordinary_declarator_suffix(tokens, close + 1);
    const std::optional<std::size_t> scanned =
        scan_function_clauses(stream, first_clause, engine, verified.clauses, /*report_decreases_unsupported=*/true);
    if (!scanned.has_value()) {
        return false;
    }
    std::size_t cursor = *scanned;

    check_clause_sequence(verified.clauses, engine);
    const auto ensures_count = std::ranges::count_if(
        verified.clauses, [](const Clause& clause) { return clause.kind == ClauseKind::Ensures; });
    if (ensures_count > 1) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "verified function '" + std::string(tokens[*name].text) + "' has " + std::to_string(ensures_count) +
                   " ensures clauses",
               "a verified function has exactly one ensures clause");
        return false;
    }
    const bool declaration_only = cursor < tokens.size() && tokens[cursor].is_punctuator(";");
    if (cursor >= tokens.size() || (!tokens[cursor].is_punctuator("{") && !declaration_only)) {
        report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
               "verified function '" + std::string(tokens[*name].text) + "' is declared but not defined here",
               "its obligation comes from the body, so this implementation verifies a "
               "function where it is defined");
        return false;
    }
    const std::size_t body_close = declaration_only ? cursor : matching_brace(tokens, cursor);
    if (body_close >= tokens.size()) {
        return false;
    }

    verified.keyword = tokens[index].span;
    verified.keyword_location = stream.location_of(tokens[index]);
    // The header ends at its closing `>`, which is where the declaration's own
    // specifiers begin. Taking it to the `verified` keyword instead would carry
    // any `inline` or `static` between them into the generated probe, where
    // they do not belong.
    const std::size_t declaration_start = specifiers_start(tokens, index);
    if (const std::optional<std::size_t> header = template_header_start(tokens, declaration_start);
        header.has_value()) {
        verified.template_header = source::ByteSpan{tokens[*header].span.offset, tokens[declaration_start].span.offset -
                                                                                     tokens[*header].span.offset};
        // `template <>` declares no parameters, so the `<` is immediately
        // followed by its `>`: an explicit specialization rather than a
        // template.
        verified.explicit_specialization = *header + 2 < tokens.size() && tokens[*header + 1].is_punctuator("<") &&
                                           tokens[*header + 2].is_punctuator(">");
    }
    verified.function_name = std::string(tokens[*name].text);
    verified.function_location = stream.location_of(tokens[*name]);
    verified.function_offset = tokens[*name].span.offset;
    verified.return_type =
        source::ByteSpan{tokens[type_start].span.offset, tokens[*name].span.offset - tokens[type_start].span.offset};
    verified.parameters =
        source::ByteSpan{tokens[open].span.end(), tokens[close].span.offset - tokens[open].span.end()};
    verified.clause_region = source::ByteSpan{tokens[first_clause].span.offset,
                                              tokens[cursor].span.offset - tokens[first_clause].span.offset};
    verified.body_end = tokens[body_close].span.end();
    verified.body_end_line = tokens[body_close].line;
    verified.body_end_column = tokens[body_close].column + 1;
    if (!declaration_only) {
        verified.body_open = tokens[cursor].span.end();
        verified.body_open_line = tokens[cursor].line;
        verified.body_open_column = tokens[cursor].column + 1;
    }

    // The body is walked as usual, so anything inside it is recognized exactly
    // as it would be in an ordinary function.
    next_index = declaration_only ? cursor + 1 : cursor;
    return true;
}

enum class ScopeKind : std::uint8_t {
    Namespace,
    Class,
    Block,
};

// Classifies the scope a '{' opens by looking back at the declaration it
// belongs to. C++L declarations are only recognized at namespace scope in this
// implementation; elsewhere they are diagnosed rather than half-handled
// (GRAMMAR.md 36).
ScopeKind scope_kind_before(const std::vector<Token>& tokens, std::size_t brace) {
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

// Whether a ',' separates components at the top level of `(` ... `)`, which is
// what makes a `decreases` measure a lexicographic list rather than one
// expression. Commas nested in a call's arguments or a braced list do not.
bool has_top_level_comma(const std::vector<Token>& tokens, std::size_t open, std::size_t close) {
    std::size_t depth = 0;
    for (std::size_t index = open + 1; index < close; ++index) {
        const Token& token = tokens[index];
        if (token.is_punctuator("(") || token.is_punctuator("[") || token.is_punctuator("{")) {
            ++depth;
        } else if (token.is_punctuator(")") || token.is_punctuator("]") || token.is_punctuator("}")) {
            --depth;
        } else if (depth == 0 && token.is_punctuator(",")) {
            return true;
        }
    }
    return false;
}

bool is_loop_clause(const std::vector<Token>& tokens, std::size_t index) {
    return index + 1 < tokens.size() &&
           (tokens[index].is_identifier("invariant") || tokens[index].is_identifier("decreases")) &&
           tokens[index + 1].is_punctuator("(");
}

enum class LoopClauses : std::uint8_t {
    None,
    Recognized,
    Refused,
};

// `while (c)` or `for (...)` followed by loop specification clauses and a block
// (GRAMMAR.md 25, 26). The clauses are C++L only where the tokens cannot be
// C++: `invariant (x) { ... };` declares `x` when `invariant` names a type, so a
// lone single-identifier invariant before a block that `;` follows is left to
// C++ (SPEC.md 3.1).
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
    if (written.size() == 1 && tokens[written[0].keyword].is_identifier("invariant") &&
        written[0].close == written[0].keyword + 3 && tokens[written[0].keyword + 2].kind == TokenKind::Identifier &&
        body_close + 1 < tokens.size() && tokens[body_close + 1].is_punctuator(";")) {
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
            // TERMINATION-004). Only a single measure is verified here, and a
            // list is refused rather than read as its first component.
            if (has_top_level_comma(tokens, clause.keyword + 1, clause.close)) {
                report(engine, stream, keyword, diagnostics::Category::UnsupportedSemantics,
                       "a lexicographic 'decreases' list is not verified by this implementation",
                       "state one measure; the requested obligation must not be accepted unchecked");
                outcome = LoopClauses::Refused;
                continue;
            }
            loop.decreases = Clause{ClauseKind::Decreases, keyword.span, measure, stream.location_of(keyword)};
            loop.measure_location = stream.location_of(tokens[clause.keyword + 2]);
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

// Where a `cases` or `decompose` statement beginning at `index` ends: the index
// of the `}` closing its arms, when the tokens have the statement's one shape, a
// subject and then a braced arm list (GRAMMAR.md 5.7).
std::optional<std::size_t> split_statement_end(const std::vector<Token>& tokens, std::size_t index) {
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

} // namespace

std::string describe(ClauseKind kind) {
    switch (kind) {
        case ClauseKind::Decreases:
            return "decreases";
        case ClauseKind::Proves:
            return "proves";
        case ClauseKind::Ensures:
            return "ensures";
        case ClauseKind::Expects:
            return "expects";
        case ClauseKind::Invariant:
            return "invariant";
    }
    return "unknown";
}

std::string describe(ProofStatementKind kind) {
    switch (kind) {
        case ProofStatementKind::Reflexivity:
            return "refl";
        case ProofStatementKind::Exact:
            return "exact";
        case ProofStatementKind::Apply:
            return "apply";
        case ProofStatementKind::Assume:
            return "assume";
        case ProofStatementKind::Rewrite:
            return "rewrite";
        case ProofStatementKind::Contradiction:
            return "contradiction";
        case ProofStatementKind::Cases:
            return "cases";
        case ProofStatementKind::Decompose:
            return "decompose";
        case ProofStatementKind::Induction:
            return "induction";
        case ProofStatementKind::Unread:
            return "unread";
    }
    return "unknown";
}

std::optional<ProofStatementKind> statement_keyword(std::string_view word) {
    for (const ProofStatementKind kind :
         {ProofStatementKind::Reflexivity, ProofStatementKind::Exact, ProofStatementKind::Apply,
          ProofStatementKind::Assume, ProofStatementKind::Rewrite, ProofStatementKind::Contradiction,
          ProofStatementKind::Cases, ProofStatementKind::Decompose, ProofStatementKind::Induction}) {
        if (describe(kind) == word) {
            return kind;
        }
    }
    return std::nullopt;
}

bool names_evidence(ProofStatementKind kind) {
    return kind == ProofStatementKind::Exact || kind == ProofStatementKind::Apply ||
           kind == ProofStatementKind::Rewrite || kind == ProofStatementKind::Contradiction;
}

std::vector<ClauseKind> clauses_admitted(ClauseOwner owner, const std::vector<Clause>& written, std::size_t at) {
    // What each declaration reads as a clause at all: `try_law` refuses
    // `ensures` and `decreases`, a proof has only its claim, and a verified
    // function refuses `proves` and does not verify `decreases`.
    std::vector<ClauseKind> accepted;
    switch (owner) {
        case ClauseOwner::Law:
            accepted = {ClauseKind::Expects, ClauseKind::Proves};
            break;
        case ClauseOwner::Proof:
            accepted = {ClauseKind::Proves};
            break;
        case ClauseOwner::VerifiedFunction:
            accepted = {ClauseKind::Expects, ClauseKind::Ensures};
            break;
    }
    // A kind is admitted where the clauses with it written there pass the
    // check `check_clause_sequence` makes.
    std::vector<ClauseKind> admitted;
    for (const ClauseKind kind : accepted) {
        std::vector<ClauseKind> sequence;
        for (const Clause& clause : written) {
            if (clause.keyword.offset < at) {
                sequence.push_back(clause.kind);
            }
        }
        sequence.push_back(kind);
        for (const Clause& clause : written) {
            if (clause.keyword.offset >= at) {
                sequence.push_back(clause.kind);
            }
        }
        std::vector<ClauseKind> seen;
        bool conclusion = false;
        const bool in_order = std::ranges::all_of(sequence, [&](ClauseKind next) {
            const bool fits = !out_of_sequence(next, seen, conclusion).has_value();
            seen.push_back(next);
            conclusion = conclusion || next == ClauseKind::Ensures || next == ClauseKind::Proves;
            return fits;
        });
        if (in_order) {
            admitted.push_back(kind);
        }
    }
    return admitted;
}

bool detail::declaration_may_begin(const std::vector<Token>& tokens, std::size_t index) {
    return at_declaration_start(tokens, index);
}

const Clause* LawDeclaration::proposition() const {
    for (const Clause& clause : clauses) {
        if (clause.kind == ClauseKind::Proves) {
            return &clause;
        }
    }
    return nullptr;
}

const Clause* LawDeclaration::premise() const {
    for (const Clause& clause : clauses) {
        if (clause.kind == ClauseKind::Expects) {
            return &clause;
        }
    }
    return nullptr;
}

const Clause* VerifiedFunction::postcondition() const {
    for (const Clause& clause : clauses) {
        if (clause.kind == ClauseKind::Ensures) {
            return &clause;
        }
    }
    return nullptr;
}

std::vector<const Clause*> VerifiedFunction::preconditions() const {
    std::vector<const Clause*> found;
    for (const Clause& clause : clauses) {
        if (clause.kind == ClauseKind::Expects) {
            found.push_back(&clause);
        }
    }
    return found;
}

Syntax recognize(const TokenStream& stream, diagnostics::Engine& engine, RecognitionMode mode) {
    const std::vector<Token>& tokens = stream.tokens();
    Syntax syntax;

    std::vector<ScopeKind> scopes;
    const auto at_namespace_scope = [&scopes] {
        return std::ranges::all_of(scopes, [](ScopeKind kind) { return kind == ScopeKind::Namespace; });
    };
    // GRAMMAR.md 36/38: Laws, proofs and verified member contracts also have
    // class scope. This implementation's semantic layer (elaboration,
    // obligations) does not yet accept a class-scope contract - Compile mode
    // keeps rejecting one exactly as before, unchanged by this predicate -
    // but the formatter (Edit mode) still needs to see and canonically lay
    // out the construct a developer wrote, the same way it lays out any
    // other syntactically well-formed but semantically unsupported input.
    const auto at_layout_scope = [&scopes] {
        return std::ranges::all_of(
            scopes, [](ScopeKind kind) { return kind == ScopeKind::Namespace || kind == ScopeKind::Class; });
    };
    // Edit and Draft keep what Compile refuses; Draft keeps more still.
    const bool tolerant = mode != RecognitionMode::Compile;

    // The token range of each verified body, so a loop's clauses can be tied to
    // the function whose obligations they become.
    struct VerifiedBody {
        std::size_t open = 0;
        std::size_t close = 0;
        std::size_t function = 0;
    };
    std::vector<VerifiedBody> verified_bodies;

    // Statements spelled `contradiction name;` or `contradiction name(...);` in
    // a function body. Which of them are claims is decided once the whole unit
    // has been read, because that depends on every other use of the word.
    struct Written {
        std::size_t keyword = 0;
        std::size_t terminator = 0;
    };
    std::vector<Written> written_contradictions;
    // Statements spelled `cases subject { ... }` or `decompose subject { ... }`
    // in a function body, decided like claims once the unit has been read. Their
    // arms are read whole, so a claim inside one is the split's, never a
    // statement of its own.
    std::vector<Written> written_splits;

    std::size_t index = 0;
    while (index < tokens.size() && tokens[index].kind != TokenKind::EndOfFile) {
        if (tokens[index].is_punctuator("{")) {
            scopes.push_back(scope_kind_before(tokens, index));
            ++index;
            continue;
        }
        if (tokens[index].is_punctuator("}")) {
            if (!scopes.empty()) {
                scopes.pop_back();
            }
            ++index;
            continue;
        }

        if ((tokens[index].is_identifier("while") || tokens[index].is_identifier("for")) && index + 1 < tokens.size() &&
            tokens[index + 1].is_punctuator("(")) {
            LoopSpecification loop;
            std::size_t next = index + 1;
            const std::size_t close = matching_parenthesis(tokens, index + 1);
            const LoopClauses found = close >= tokens.size()
                                          ? LoopClauses::None
                                          : try_loop_clauses(stream, index, close + 1, engine, loop, next);
            if (found != LoopClauses::None) {
                const auto body = std::ranges::find_if(verified_bodies, [index](const VerifiedBody& candidate) {
                    return candidate.open < index && index < candidate.close;
                });
                if (body == verified_bodies.end()) {
                    report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
                           "a loop invariant outside a verified function would not be checked",
                           "mark the enclosing function 'verified' so its loop invariants become obligations");
                } else if (found == LoopClauses::Recognized) {
                    loop.function_index = body->function;
                    syntax.loops.push_back(std::move(loop));
                }
                index = next;
                continue;
            }
        }

        // do loop-clauses compound-statement while (condition);  (GRAMMAR.md 26)
        if (tokens[index].is_identifier("do") && index + 1 < tokens.size()) {
            LoopSpecification loop;
            std::size_t next = index + 1;
            const LoopClauses found = try_loop_clauses(stream, index, index + 1, engine, loop, next);
            if (found != LoopClauses::None) {
                const auto body = std::ranges::find_if(verified_bodies, [index](const VerifiedBody& candidate) {
                    return candidate.open < index && index < candidate.close;
                });
                if (body == verified_bodies.end()) {
                    report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
                           "a loop invariant outside a verified function would not be checked",
                           "mark the enclosing function 'verified' so its loop invariants become obligations");
                } else if (found == LoopClauses::Recognized) {
                    loop.function_index = body->function;
                    syntax.loops.push_back(std::move(loop));
                }
                index = next;
                continue;
            }
        }

        if (tokens[index].is_identifier("contradiction") && !scopes.empty() && scopes.back() == ScopeKind::Block &&
            at_statement_start(tokens, index)) {
            if (const std::optional<std::size_t> terminator = contradiction_statement_end(tokens, index)) {
                written_contradictions.push_back(Written{index, *terminator});
                index = *terminator + 1;
                continue;
            }
        }

        if ((tokens[index].is_identifier("cases") || tokens[index].is_identifier("decompose")) && !scopes.empty() &&
            scopes.back() == ScopeKind::Block && at_statement_start(tokens, index)) {
            if (const std::optional<std::size_t> close = split_statement_end(tokens, index)) {
                written_splits.push_back(Written{index, *close});
                index = *close + 1;
                continue;
            }
        }

        if (!at_declaration_start(tokens, index)) {
            ++index;
            continue;
        }

        // type name [(indices)] = base where (predicate);  (GRAMMAR.md 14, 16)
        if (tokens[index].is_identifier("type")) {
            RefinementType refinement;
            std::size_t next = index + 1;
            if (try_refinement_type(stream, index, engine, refinement, next)) {
                if (!refinement.name.empty()) {
                    if (at_namespace_scope()) {
                        syntax.refinement_types.push_back(std::move(refinement));
                    } else {
                        report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
                               "refinement type '" + refinement.name + "' is declared outside namespace scope",
                               "this implementation recognizes refinement types at namespace scope only");
                    }
                }
                index = next;
                continue;
            }
        }

        // trusted law ... ;  (GRAMMAR.md 24, SPEC.md 27)
        //
        // The proposition is assumed rather than proved. It is recorded as an
        // explicit trusted assumption, counted in the trust report, and never
        // reported as proven.
        if (tokens[index].is_identifier("trusted") && index + 1 < tokens.size() &&
            tokens[index + 1].is_identifier("law")) {
            LawDeclaration law;
            ProofDeclaration body;
            std::size_t next = index + 2;
            if (try_law(stream, index + 1, engine, law, next, body, mode)) {
                if (!body.name.empty()) {
                    report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
                           "a trusted Law ends with ';': an assumption cannot also have a proof body");
                    law.name.clear();
                }
                if (!law.name.empty()) {
                    if (at_namespace_scope()) {
                        law.trusted = true;
                        law.keyword_location = stream.location_of(tokens[index]);
                        // `try_law` measured the declaration from `law`, so the
                        // span must be widened to cover `trusted` as well: the
                        // projector blanks exactly this span, and a keyword left
                        // behind would reach Clang as ordinary C++.
                        law.range.span = source::ByteSpan{tokens[index].span.offset,
                                                          law.range.span.end() - tokens[index].span.offset};
                        syntax.laws.push_back(std::move(law));
                    } else {
                        report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
                               "a trusted law must be declared at namespace scope",
                               "a trusted assumption is a unit-level declaration, so that the trust report can "
                               "name it");
                    }
                }
                index = next;
                continue;
            }
            // A draft's `trusted law name(...)` with no clause yet. The tokens
            // are still ordinary C++, so recognition goes on through them.
            if (law.completeness == Completeness::AwaitingClause && at_namespace_scope()) {
                law.trusted = true;
                law.keyword_location = stream.location_of(tokens[index]);
                law.range.span =
                    source::ByteSpan{tokens[index].span.offset, law.range.span.end() - tokens[index].span.offset};
                syntax.laws.push_back(std::move(law));
            }
        }

        if (tokens[index].is_identifier("law")) {
            LawDeclaration law;
            ProofDeclaration body;
            std::size_t next = index + 1;
            if (try_law(stream, index, engine, law, next, body, mode)) {
                if (!law.name.empty()) {
                    if (!at_namespace_scope()) {
                        report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
                               "law '" + law.name + "' is declared outside namespace scope",
                               "this implementation recognizes laws at namespace scope only");
                    }
                    if (at_namespace_scope() || (tolerant && at_layout_scope())) {
                        if (!body.name.empty()) {
                            body.inline_law = syntax.laws.size();
                            syntax.proofs.push_back(std::move(body));
                        }
                        syntax.laws.push_back(std::move(law));
                    }
                }
                index = next;
                continue;
            }
            if (law.completeness == Completeness::AwaitingClause && at_layout_scope()) {
                syntax.laws.push_back(std::move(law));
            }
        }

        // proof name(...) proves (...) { ... }  (GRAMMAR.md 4)
        if (tokens[index].is_identifier("proof")) {
            ProofDeclaration proof;
            std::size_t next = index + 1;
            if (try_proof(stream, index, engine, proof, next, mode)) {
                if (!proof.name.empty()) {
                    if (!at_namespace_scope()) {
                        report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
                               "proof '" + proof.name + "' is declared outside namespace scope",
                               "this implementation recognizes proofs at namespace scope only");
                    }
                    if (at_namespace_scope() || (tolerant && at_layout_scope())) {
                        syntax.proofs.push_back(std::move(proof));
                    }
                }
                index = next;
                continue;
            }
            if (proof.completeness == Completeness::AwaitingClause && at_layout_scope()) {
                syntax.proofs.push_back(std::move(proof));
            }
        }

        if (tokens[index].is_identifier("template") && at_namespace_scope() && at_declaration_start(tokens, index)) {
            ExplicitInstantiation instantiation;
            std::size_t next = index + 1;
            if (try_explicit_instantiation(stream, tokens, index, instantiation, next)) {
                syntax.explicit_instantiations.push_back(std::move(instantiation));
                index = next;
                continue;
            }
        }

        if (tokens[index].is_identifier("verified") && specifier_introduces_declaration(tokens, index)) {
            VerifiedFunction verified;
            std::size_t next = index + 1;
            if (try_verified(stream, index, engine, verified, next)) {
                if (!at_namespace_scope()) {
                    report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
                           "'verified' is applied outside namespace scope",
                           "this implementation verifies functions at namespace scope only");
                }
                if (at_namespace_scope() || (tolerant && at_layout_scope())) {
                    // `verified pure` is both: the contract is discharged here,
                    // and the function is still a candidate definition for the
                    // formal core.
                    if (tokens[index + 1].is_identifier("pure")) {
                        PureMarker marker;
                        marker.keyword = tokens[index + 1].span;
                        marker.keyword_location = stream.location_of(tokens[index + 1]);
                        marker.function_name = verified.function_name;
                        marker.function_location = verified.function_location;
                        marker.function_offset = verified.function_offset;
                        syntax.pure_markers.push_back(std::move(marker));
                    }
                    syntax.verified_functions.push_back(std::move(verified));
                    // A declaration without a body owns none. `next` is then past
                    // its `;`, and the braces after it are another function's.
                    if (next < tokens.size() && tokens[next].is_punctuator("{")) {
                        verified_bodies.push_back(
                            VerifiedBody{next, matching_brace(tokens, next), syntax.verified_functions.size() - 1});
                    }
                }
            }
            index = next;
            continue;
        }

        if (tokens[index].is_identifier("pure") && specifier_introduces_declaration(tokens, index)) {
            const std::optional<std::size_t> name = find_declarator_name(tokens, index);
            if (!name.has_value()) {
                report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
                       "the 'pure' specifier applies to a function declaration",
                       "no function declarator follows this specifier");
            } else {
                std::size_t clause_index = 0;
                if (has_specification_clause(tokens, *name, clause_index)) {
                    report(engine, stream, tokens[clause_index], diagnostics::Category::UnsupportedSemantics,
                           "a contract on a function that is not 'verified' would not be "
                           "checked",
                           "mark the function 'verified' so its contract becomes an "
                           "obligation, or state the property as a law over it");
                    // This clause is never an obligation (Compile mode never
                    // accepts it, unchanged by the diagnostic above - see
                    // `Syntax::unchecked_clauses`), but the formatter/style
                    // checker still has to lay out whatever clause syntax was
                    // written, in every `RecognitionMode`, the same way it
                    // lays out any other syntactically well-formed,
                    // semantically unsupported construct.
                    record_unchecked_clauses(stream, index, *name, engine, syntax);
                } else if (!at_namespace_scope()) {
                    report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
                           "'pure' is applied outside namespace scope",
                           "this implementation recognizes pure functions at namespace scope "
                           "only");
                } else {
                    PureMarker marker;
                    marker.keyword = tokens[index].span;
                    marker.keyword_location = stream.location_of(tokens[index]);
                    marker.function_name = std::string(tokens[*name].text);
                    marker.function_location = stream.location_of(tokens[*name]);
                    marker.function_offset = tokens[*name].span.offset;
                    syntax.pure_markers.push_back(std::move(marker));
                }
            }
            ++index;
            continue;
        }

        // Any other function declaration carrying clauses. The specifier in
        // front of it is not one this implementation reads -- `inline`,
        // `static`, `constexpr`, or a macro that expands to `verified`, whose
        // expansion the formatter never sees because it lexes the source as
        // written rather than the preprocessed text the compiler recognizes.
        //
        // Nothing here is checked: this claims no contract and emits no
        // obligation, and the compiler's own reading of the declaration is
        // untouched. It exists so that clause syntax a developer actually
        // wrote is laid out rather than silently skipped, which is the same
        // reason the `pure` path above records one. Without it the formatter
        // is not idempotent in the way its users rely on: whether a clause
        // gets canonical layout would depend on which specifier happens to
        // precede it.
        //
        // No diagnostic accompanies this. The `pure` path's "would not be
        // checked" report is about `pure`, a C++L specifier whose author
        // plainly meant the contract to mean something. Here the leading
        // token may be an ordinary C++ specifier or an unexpanded macro, and
        // the Compile-mode recognizer reaches the same declaration through
        // the preprocessed stream where the macro is already `verified` -- so
        // warning would fire on correct, checked code.
        //
        // Two guards keep this from claiming a declaration that is already
        // spoken for. `specifiers_start` is the declaration's own first token,
        // and `at_declaration_start` is true both there and at each specifier
        // after it, so recording anywhere else would file two overlapping
        // regions for one declaration. `has_cppl_keyword` then yields to the
        // branches above, which read the declaration properly.
        std::size_t clause_index = 0;
        if (const std::optional<std::size_t> name = find_declarator_name(tokens, index);
            name.has_value() && specifiers_start(tokens, index) == index && !has_cppl_keyword(tokens, index, *name) &&
            has_specification_clause(tokens, *name, clause_index)) {
            record_unchecked_clauses(stream, index, *name, engine, syntax);
        }

        ++index;
    }

    // C++ first (SPEC.md 3.1, WORD-002). `contradiction name;` declares a
    // variable wherever `contradiction` names a type, and only Clang knows what
    // a name denotes. So a statement of that spelling is a claim only in a unit
    // that uses the word for nothing else, where it cannot be ordinary C++. The
    // word's uses inside laws and proofs are C++L's own and do not count; any
    // other use, a declaration in a header included, does. So are its uses
    // inside a split's arms, which belong to that split.
    const auto in_proof = [&syntax](const Token& token) {
        const auto covers = [&token](const source::ByteSpan& span) {
            return token.span.offset >= span.offset && token.span.offset < span.end();
        };
        return std::ranges::any_of(syntax.proofs,
                                   [&covers](const ProofDeclaration& proof) { return covers(proof.range.span); }) ||
               std::ranges::any_of(syntax.laws,
                                   [&covers](const LawDeclaration& law) { return covers(law.range.span); });
    };
    const auto in_split = [&written_splits](std::size_t at) {
        return std::ranges::any_of(
            written_splits, [at](const Written& written) { return written.keyword <= at && at <= written.terminator; });
    };
    if (!written_contradictions.empty()) {
        std::optional<std::size_t> other;
        for (std::size_t at = 0; at < tokens.size() && !other.has_value(); ++at) {
            if (tokens[at].is_identifier("contradiction") && !in_proof(tokens[at]) && !in_split(at) &&
                std::ranges::none_of(written_contradictions,
                                     [at](const Written& written) { return written.keyword == at; })) {
                other = at;
            }
        }

        for (const Written& written : written_contradictions) {
            const auto body = std::ranges::find_if(verified_bodies, [&written](const VerifiedBody& candidate) {
                return candidate.open < written.keyword && written.keyword < candidate.close;
            });
            if (other.has_value()) {
                if (body != verified_bodies.end()) {
                    diagnostics::Diagnostic diagnostic;
                    diagnostic.severity = diagnostics::Severity::Warning;
                    diagnostic.category = diagnostics::Category::CpplSyntax;
                    diagnostic.message = "'contradiction' is also a name in this translation unit, so this statement "
                                         "is ordinary C++, not a claim that the path cannot occur";
                    diagnostic.location = stream.location_of(tokens[written.keyword]);
                    diagnostic.notes.push_back(
                        diagnostics::Note{"the name is used here", stream.location_of(tokens[*other])});
                    engine.report(std::move(diagnostic));
                }
                continue;
            }
            if (body == verified_bodies.end()) {
                report(engine, stream, tokens[written.keyword], diagnostics::Category::UnsupportedSemantics,
                       "a claim that a path cannot occur is checked only in a verified function",
                       "mark the enclosing function 'verified' so its contradiction becomes an obligation");
                continue;
            }
            std::vector<ProofStatement> statements;
            if (!read_proof_statements(stream, written.keyword - 1, written.terminator + 1, engine, statements, 0)) {
                continue;
            }
            const Token& keyword = tokens[written.keyword];
            const Token& terminator = tokens[written.terminator];
            PathContradiction claim;
            claim.function_index = body->function;
            claim.statement = std::move(statements.front());
            claim.span = source::ByteSpan{keyword.span.offset, terminator.span.end() - keyword.span.offset};
            claim.erased = source::ByteSpan{keyword.span.offset, terminator.span.offset - keyword.span.offset};
            claim.end_line = terminator.line;
            claim.end_column = terminator.column + 1;
            syntax.path_contradictions.push_back(std::move(claim));
        }
    }

    // A split on a runtime path follows the same rule, word by word: `cases x
    // {...}` is a declaration with a braced initializer wherever `cases` names a
    // type (SPEC.md 3.1, CASE-017).
    if (!written_splits.empty()) {
        const auto other_use = [&](std::string_view word) -> std::optional<std::size_t> {
            for (std::size_t at = 0; at < tokens.size(); ++at) {
                if (tokens[at].is_identifier(word) && !in_proof(tokens[at]) && !in_split(at)) {
                    return at;
                }
            }
            return std::nullopt;
        };
        const std::optional<std::size_t> other_cases = other_use("cases");
        const std::optional<std::size_t> other_decompose = other_use("decompose");

        for (const Written& written : written_splits) {
            const Token& keyword = tokens[written.keyword];
            const std::optional<std::size_t>& other = keyword.is_identifier("cases") ? other_cases : other_decompose;
            const auto body = std::ranges::find_if(verified_bodies, [&written](const VerifiedBody& candidate) {
                return candidate.open < written.keyword && written.keyword < candidate.close;
            });
            if (other.has_value()) {
                if (body != verified_bodies.end()) {
                    diagnostics::Diagnostic diagnostic;
                    diagnostic.severity = diagnostics::Severity::Warning;
                    diagnostic.category = diagnostics::Category::CpplSyntax;
                    diagnostic.message = "'" + std::string(keyword.text) +
                                         "' is also a name in this translation unit, so this statement is ordinary "
                                         "C++, not a case split";
                    diagnostic.location = stream.location_of(keyword);
                    diagnostic.notes.push_back(
                        diagnostics::Note{"the name is used here", stream.location_of(tokens[*other])});
                    engine.report(std::move(diagnostic));
                }
                continue;
            }
            if (body == verified_bodies.end()) {
                report(engine, stream, keyword, diagnostics::Category::UnsupportedSemantics,
                       "a case split on a runtime path is checked only in a verified function",
                       "mark the enclosing function 'verified' so its case split takes part in its verification");
                continue;
            }
            std::vector<ProofStatement> statements;
            if (!read_proof_statements(stream, written.keyword - 1, written.terminator + 1, engine, statements, 0) ||
                statements.size() != 1 || !admit_split_arms(engine, statements.front())) {
                continue;
            }
            const Token& close = tokens[written.terminator];
            PathCaseSplit split;
            split.function_index = body->function;
            split.statement = std::move(statements.front());
            split.span = source::ByteSpan{keyword.span.offset, close.span.end() - keyword.span.offset};
            split.end_line = close.line;
            split.end_column = close.column + 1;

            std::vector<std::pair<const ProofStatement*, const ProofArm*>> claims;
            split_claims(split.statement, claims);
            for (const auto& [statement, omitted] : claims) {
                PathContradiction claim;
                claim.function_index = body->function;
                claim.statement = *statement;
                claim.span = statement->keyword;
                claim.erased = source::ByteSpan{statement->keyword.offset, 0};
                claim.split = syntax.path_splits.size();
                if (omitted != nullptr) {
                    claim.omitted = omitted->spelling;
                }
                split.claims.push_back(syntax.path_contradictions.size());
                syntax.path_contradictions.push_back(std::move(claim));
            }
            syntax.path_splits.push_back(std::move(split));
        }
    }

    return syntax;
}

} // namespace cppl::frontend
