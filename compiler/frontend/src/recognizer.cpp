#include "cppl/frontend/syntax.hpp"

#include <algorithm>
#include <array>
#include <optional>

namespace cppl::frontend {

namespace {

constexpr std::array<std::string_view, 23> kTypeKeywords = {
    "void",     "bool",     "char",   "char8_t", "char16_t", "char32_t", "wchar_t",  "short",
    "int",      "long",     "float",  "double",  "signed",   "unsigned", "auto",     "const",
    "volatile", "typename", "struct", "class",   "enum",     "decltype", "constexpr"};

bool is_type_keyword(const Token& token) {
    return token.kind == TokenKind::Identifier && std::ranges::find(kTypeKeywords, token.text) != kTypeKeywords.end();
}

// A declaration can begin here: at the start of the unit, or after a token that
// can only end a previous declaration, statement or label.
bool at_declaration_start(const std::vector<Token>& tokens, std::size_t index) {
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

void report(diagnostics::Engine& engine, const TokenStream& stream, const Token& token, diagnostics::Category category,
            std::string message, std::string note = {}) {
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = diagnostics::Severity::Error;
    diagnostic.category = category;
    diagnostic.message = std::move(message);
    diagnostic.location = stream.location_of(token);
    if (!note.empty()) {
        diagnostic.notes.push_back(diagnostics::Note{std::move(note), diagnostic.location});
    }
    engine.report(std::move(diagnostic));
}

// `law` introduces a Law only when a contract clause follows the parameter
// list. Up to that point the token sequence is still ordinary C++ (a function
// returning a type named `law`, for instance), so nothing is reinterpreted
// until the clause makes the ordinary C++ reading impossible (SPEC.md 3.1).
bool try_law(const TokenStream& stream, std::size_t index, diagnostics::Engine& engine, LawDeclaration& law,
             std::size_t& next_index) {
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

    std::size_t cursor = close + 1;
    if (cursor >= tokens.size() || !clause_kind(tokens[cursor]).has_value()) {
        return false; // still ordinary C++
    }

    law.name = std::string(tokens[index + 1].text);
    law.range.begin = stream.location_of(tokens[index + 1]);
    law.keyword_location = stream.location_of(tokens[index]);
    law.parameters =
        source::ByteSpan{tokens[index + 2].span.end(), tokens[close].span.offset - tokens[index + 2].span.end()};

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
        clause.location = stream.location_of(tokens[cursor]);
        clause.expression = source::ByteSpan{tokens[cursor + 1].span.end(),
                                             tokens[clause_close].span.offset - tokens[cursor + 1].span.end()};
        if (stream.spelling(clause.expression).find_first_not_of(" \t\r\n") == std::string_view::npos) {
            report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                   "'" + std::string(tokens[cursor].text) + "' requires an expression");
            malformed = true;
        }
        law.clauses.push_back(clause);
        cursor = clause_close + 1;
    }

    if (cursor >= tokens.size() || !tokens[cursor].is_punctuator(";")) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax, "a law declaration ends with ';'",
               "a law states a proposition and has no body");
        next_index = cursor;
        return true;
    }

    law.range.span = source::ByteSpan{tokens[index].span.offset, tokens[cursor].span.end() - tokens[index].span.offset};
    law.end_line = tokens[cursor].line;
    next_index = cursor + 1;

    const auto ensures_count =
        std::ranges::count_if(law.clauses, [](const Clause& clause) { return clause.kind == ClauseKind::Ensures; });
    if (ensures_count == 0) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "law '" + law.name + "' states no proposition", "a law requires exactly one ensures clause");
        malformed = true;
    } else if (ensures_count > 1) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "law '" + law.name + "' has " + std::to_string(ensures_count) + " ensures clauses",
               "a law has exactly one ensures clause");
        malformed = true;
    }

    if (malformed) {
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
                   " = int where(...)'");
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
// expression meaning in one place (SPEC.md 7.3, ARCHITECTURE.md 11).
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
                           diagnostics::Engine& engine, std::vector<ProofStatement>& statements, unsigned nesting = 0) {
    const std::vector<Token>& tokens = stream.tokens();
    if (nesting > 32) {
        report(engine, stream, tokens[body_open], diagnostics::Category::CpplSyntax,
               "proof arms nest deeper than the supported limit");
        return false;
    }

    std::size_t cursor = body_open + 1;
    while (cursor < body_close) {
        const Token& token = tokens[cursor];

        if (token.is_identifier("cases") && cursor + 2 < body_close &&
            tokens[cursor + 1].kind == TokenKind::Identifier && tokens[cursor + 2].is_punctuator("{")) {
            ProofStatement statement;
            statement.kind = ProofStatementKind::Cases;
            statement.reference = std::string(tokens[cursor + 1].text);
            statement.proposition = tokens[cursor + 1].span;
            statement.location = stream.location_of(token);
            const std::size_t end = matching_brace(tokens, cursor + 2);
            if (end >= body_close) {
                report(engine, stream, token, diagnostics::Category::CpplSyntax, "unterminated cases statement");
                return false;
            }
            cursor += 3;
            bool malformed = false;
            while (cursor < end) {
                malformed = true;
                ProofArm arm;
                arm.location = stream.location_of(tokens[cursor]);
                const std::size_t start = cursor;
                if (tokens[cursor].is_punctuator("::"))
                    ++cursor;
                if (cursor >= end || tokens[cursor].kind != TokenKind::Identifier)
                    break;
                ++cursor;
                while (cursor + 1 < end && tokens[cursor].is_punctuator("::") &&
                       tokens[cursor + 1].kind == TokenKind::Identifier)
                    cursor += 2;
                arm.label = {tokens[start].span.offset, tokens[cursor - 1].span.end() - tokens[start].span.offset};
                arm.residual = cursor == start + 1 && tokens[start].is_identifier("unnamed");
                if (tokens[start].is_identifier("_")) {
                    report(engine, stream, tokens[start], diagnostics::Category::CpplSyntax,
                           "cases has no wildcard arm");
                    return false;
                }
                if (!arm.residual && cursor == start + 1) {
                    report(engine, stream, tokens[start], diagnostics::Category::UnsupportedSemantics,
                           "enum case labels must be qualified, as in 'State::idle'");
                    return false;
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
                if (close >= end || !read_proof_statements(stream, cursor, close, engine, arm.statements, nesting + 1))
                    return false;
                statement.arms.push_back(std::move(arm));
                malformed = false;
                cursor = close + 1;
            }
            if (malformed || cursor != end || statement.arms.empty() || statement.arms.size() > 64) {
                report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                       "cases requires 1 to 64 arms of the form 'label(binders) => { proof statements }'");
                return false;
            }
            statements.push_back(std::move(statement));
            cursor = end + 1;
            continue;
        }

        if (token.is_identifier("refl") && cursor + 1 < body_close && tokens[cursor + 1].is_punctuator(";")) {
            ProofStatement statement;
            statement.kind = ProofStatementKind::Reflexivity;
            statement.location = stream.location_of(token);
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
            statement.proposition = source::ByteSpan{
                tokens[cursor + 3].span.offset, tokens[terminator - 1].span.end() - tokens[cursor + 3].span.offset};
            statement.proposition_location = stream.location_of(tokens[cursor + 3]);
            statement.location = stream.location_of(token);
            statements.push_back(std::move(statement));
            cursor = terminator + 1;
            continue;
        }

        const bool is_exact = token.is_identifier("exact");
        const bool is_rewrite = token.is_identifier("rewrite");
        if ((is_exact || is_rewrite || token.is_identifier("apply")) && cursor + 2 < body_close &&
            tokens[cursor + 1].kind == TokenKind::Identifier) {
            ProofStatement statement;
            statement.kind = is_exact     ? ProofStatementKind::Exact
                             : is_rewrite ? ProofStatementKind::Rewrite
                                          : ProofStatementKind::Apply;
            statement.reference = std::string(tokens[cursor + 1].text);
            statement.location = stream.location_of(token);

            if (tokens[cursor + 2].is_punctuator(";")) {
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
               "'apply <evidence>;', 'rewrite <evidence>;' and "
               "'assume <name> : <proposition>;'. Evidence may be instantiated at arguments, "
               "as in 'exact <proof>(<expression>);'");
        return false;
    }

    return true;
}

// `proof` introduces a proof declaration only when `proves` follows the
// parameter list. Until then the token sequence is still ordinary C++ - a
// function returning a type named `proof`, for instance (SPEC.md 3.1).
bool try_proof(const TokenStream& stream, std::size_t index, diagnostics::Engine& engine, ProofDeclaration& proof,
               std::size_t& next_index) {
    const std::vector<Token>& tokens = stream.tokens();

    if (index + 2 >= tokens.size()) {
        return false;
    }
    if (tokens[index + 1].kind != TokenKind::Identifier || !tokens[index + 2].is_punctuator("(")) {
        return false;
    }

    const std::size_t close = matching_parenthesis(tokens, index + 2);
    if (close >= tokens.size() || close + 1 >= tokens.size() || !tokens[close + 1].is_identifier("proves")) {
        return false; // still ordinary C++
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

    if (proves_close + 1 >= tokens.size() || !tokens[proves_close + 1].is_punctuator("{")) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax, "a proof declaration has a body",
               "a proof constructs evidence, so it ends with '{ ... }' rather than ';'");
        next_index = proves_close + 1;
        return true;
    }

    const std::size_t body_open = proves_close + 1;
    const std::size_t body_close = matching_brace(tokens, body_open);
    if (body_close >= tokens.size()) {
        report(engine, stream, tokens[body_open], diagnostics::Category::CpplSyntax, "unterminated proof body");
        next_index = body_open + 1;
        return true;
    }

    next_index = body_close + 1;

    proof.name = std::string(tokens[index + 1].text);
    proof.range.begin = stream.location_of(tokens[index + 1]);
    proof.range.span =
        source::ByteSpan{tokens[index].span.offset, tokens[body_close].span.end() - tokens[index].span.offset};
    proof.keyword_location = stream.location_of(tokens[index]);
    proof.end_line = tokens[body_close].line;
    proof.parameters =
        source::ByteSpan{tokens[index + 2].span.end(), tokens[close].span.offset - tokens[index + 2].span.end()};
    proof.proposition = source::ByteSpan{tokens[proves + 1].span.end(),
                                         tokens[proves_close].span.offset - tokens[proves + 1].span.end()};
    proof.proposition_location = stream.location_of(tokens[proves]);

    bool malformed = stream.spelling(proof.proposition).find_first_not_of(" \t\r\n") == std::string_view::npos;
    if (malformed) {
        report(engine, stream, tokens[proves], diagnostics::Category::CpplSyntax, "'proves' requires a proposition");
    }

    if (!read_proof_statements(stream, body_open, body_close, engine, proof.statements)) {
        malformed = true;
    } else if (proof.statements.empty()) {
        report(engine, stream, tokens[index], diagnostics::Category::ProofFailure,
               "proof '" + proof.name + "' has an empty body", "a proof body must close the goal it states");
        malformed = true;
    }

    if (malformed) {
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

// Specification clauses on a function declarator are part of the language
// (GRAMMAR.md 6) but are not verified by this implementation. They are
// diagnosed rather than erased, because silently dropping a contract would
// turn a specification into nothing at all.
bool has_specification_clause(const std::vector<Token>& tokens, std::size_t name_index, std::size_t& clause_index) {
    const std::size_t open = name_index + 1;
    if (open >= tokens.size() || !tokens[open].is_punctuator("(")) {
        return false;
    }
    const std::size_t close = matching_parenthesis(tokens, open);
    if (close >= tokens.size()) {
        return false;
    }

    for (std::size_t cursor = close + 1; cursor < tokens.size(); ++cursor) {
        const Token& token = tokens[cursor];
        if (token.kind == TokenKind::EndOfFile || token.is_punctuator(";") || token.is_punctuator("{")) {
            return false;
        }
        if (is_specification_clause(token) && cursor + 1 < tokens.size() && tokens[cursor + 1].is_punctuator("(")) {
            clause_index = cursor;
            return true;
        }
    }
    return false;
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

    const std::size_t open = *name + 1;
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
    if (tokens[type_start].is_identifier("auto")) {
        report(engine, stream, tokens[type_start], diagnostics::Category::UnsupportedSemantics,
               "a deduced return type is not supported on a verified function",
               "the contract's 'result' is a value of the declared return type, so this "
               "implementation requires one to be written");
        return false;
    }

    const std::size_t first_clause = close + 1;
    std::size_t cursor = first_clause;
    while (cursor < tokens.size()) {
        const std::optional<ClauseKind> kind = clause_kind(tokens[cursor]);
        if (!kind.has_value()) {
            break;
        }
        if (cursor + 1 >= tokens.size() || !tokens[cursor + 1].is_punctuator("(")) {
            report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                   "'" + std::string(tokens[cursor].text) +
                       "' must be followed by a parenthesized specification expression");
            return false;
        }
        const std::size_t clause_close = matching_parenthesis(tokens, cursor + 1);
        if (clause_close >= tokens.size()) {
            report(engine, stream, tokens[cursor + 1], diagnostics::Category::CpplSyntax,
                   "unterminated specification expression");
            return false;
        }

        Clause clause;
        clause.kind = *kind;
        clause.location = stream.location_of(tokens[cursor]);
        clause.expression = source::ByteSpan{tokens[cursor + 1].span.end(),
                                             tokens[clause_close].span.offset - tokens[cursor + 1].span.end()};
        if (stream.spelling(clause.expression).find_first_not_of(" \t\r\n") == std::string_view::npos) {
            report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                   "'" + std::string(tokens[cursor].text) + "' requires an expression");
            return false;
        }
        verified.clauses.push_back(clause);
        cursor = clause_close + 1;
    }

    if (verified.clauses.empty()) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "verified function '" + std::string(tokens[*name].text) + "' states no contract",
               "a verified function requires an ensures clause; there is nothing else for "
               "'verified' to mean");
        return false;
    }

    const auto ensures_count = std::ranges::count_if(
        verified.clauses, [](const Clause& clause) { return clause.kind == ClauseKind::Ensures; });
    if (ensures_count != 1) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "verified function '" + std::string(tokens[*name].text) + "' has " + std::to_string(ensures_count) +
                   " ensures clauses",
               "a verified function has exactly one ensures clause");
        return false;
    }
    if (cursor >= tokens.size() || !tokens[cursor].is_punctuator("{")) {
        report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
               "verified function '" + std::string(tokens[*name].text) + "' is declared but not defined here",
               "its obligation comes from the body, so this implementation verifies a "
               "function where it is defined");
        return false;
    }
    const std::size_t body_close = matching_brace(tokens, cursor);
    if (body_close >= tokens.size()) {
        return false;
    }

    verified.keyword = tokens[index].span;
    verified.keyword_location = stream.location_of(tokens[index]);
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

    // The body is walked as usual, so anything inside it is recognized exactly
    // as it would be in an ordinary function.
    next_index = cursor;
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
// C++: `invariant(x) { ... };` declares `x` when `invariant` names a type, so a
// lone single-identifier invariant before a block that `;` follows is left to
// C++ (SPEC.md 3.1).
LoopClauses try_loop_clauses(const TokenStream& stream, std::size_t index, diagnostics::Engine& engine,
                             LoopSpecification& loop, std::size_t& next_index) {
    const std::vector<Token>& tokens = stream.tokens();
    const std::size_t close = matching_parenthesis(tokens, index + 1);
    if (close >= tokens.size() || !is_loop_clause(tokens, close + 1)) {
        return LoopClauses::None;
    }

    struct Written {
        std::size_t keyword;
        std::size_t close;
    };
    std::vector<Written> written;
    std::size_t cursor = close + 1;
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
            report(engine, stream, keyword, diagnostics::Category::UnsupportedSemantics,
                   "loop termination is not verified by this implementation",
                   "a verified loop establishes partial correctness only; 'decreases' is refused rather than "
                   "left unchecked");
            outcome = LoopClauses::Refused;
            continue;
        }
        Clause invariant;
        invariant.kind = ClauseKind::Invariant;
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

    loop.keyword_location = stream.location_of(tokens[index]);
    loop.clause_region = source::ByteSpan{tokens[written.front().keyword].span.offset,
                                          tokens[cursor].span.offset - tokens[written.front().keyword].span.offset};
    loop.body_open = tokens[cursor].span.end();
    loop.body_open_line = tokens[cursor].line;
    loop.body_open_column = tokens[cursor].column + 1;
    return outcome;
}

} // namespace

std::string describe(ClauseKind kind) {
    switch (kind) {
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
        case ProofStatementKind::Cases:
            return "cases";
    }
    return "unknown";
}

const Clause* LawDeclaration::proposition() const {
    for (const Clause& clause : clauses) {
        if (clause.kind == ClauseKind::Ensures) {
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

Syntax recognize(const TokenStream& stream, diagnostics::Engine& engine) {
    const std::vector<Token>& tokens = stream.tokens();
    Syntax syntax;

    std::vector<ScopeKind> scopes;
    const auto at_namespace_scope = [&scopes] {
        return std::ranges::all_of(scopes, [](ScopeKind kind) { return kind == ScopeKind::Namespace; });
    };

    // The token range of each verified body, so a loop's clauses can be tied to
    // the function whose obligations they become.
    struct VerifiedBody {
        std::size_t open = 0;
        std::size_t close = 0;
        std::size_t function = 0;
    };
    std::vector<VerifiedBody> verified_bodies;

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
            const LoopClauses found = try_loop_clauses(stream, index, engine, loop, next);
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

        // trusted law ... ;  (GRAMMAR.md 24)
        if (tokens[index].is_identifier("trusted") && index + 1 < tokens.size() &&
            tokens[index + 1].is_identifier("law")) {
            LawDeclaration probe;
            std::size_t next = index + 2;
            if (try_law(stream, index + 1, engine, probe, next)) {
                report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
                       "trusted laws are not supported by this implementation",
                       "a trusted law introduces an explicit assumption; this implementation "
                       "cannot yet record one, and will not silently drop it");
                index = next;
                continue;
            }
        }

        if (tokens[index].is_identifier("law")) {
            LawDeclaration law;
            std::size_t next = index + 1;
            if (try_law(stream, index, engine, law, next)) {
                if (!law.name.empty()) {
                    if (at_namespace_scope()) {
                        syntax.laws.push_back(std::move(law));
                    } else {
                        report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
                               "law '" + law.name + "' is declared outside namespace scope",
                               "this implementation recognizes laws at namespace scope only");
                    }
                }
                index = next;
                continue;
            }
        }

        // proof name(...) proves(...) { ... }  (GRAMMAR.md 4)
        if (tokens[index].is_identifier("proof")) {
            ProofDeclaration proof;
            std::size_t next = index + 1;
            if (try_proof(stream, index, engine, proof, next)) {
                if (!proof.name.empty()) {
                    if (at_namespace_scope()) {
                        syntax.proofs.push_back(std::move(proof));
                    } else {
                        report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
                               "proof '" + proof.name + "' is declared outside namespace scope",
                               "this implementation recognizes proofs at namespace scope only");
                    }
                }
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
                } else {
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
                    verified_bodies.push_back(
                        VerifiedBody{next, matching_brace(tokens, next), syntax.verified_functions.size() - 1});
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
        }

        ++index;
    }

    return syntax;
}

} // namespace cppl::frontend
