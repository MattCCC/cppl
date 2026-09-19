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
    return token.kind == TokenKind::Identifier &&
           std::ranges::find(kTypeKeywords, token.text) != kTypeKeywords.end();
}

// A declaration can begin here: at the start of the unit, or after a token that
// can only end a previous declaration, statement or label.
bool at_declaration_start(const std::vector<Token>& tokens, std::size_t index) {
    if (index == 0) {
        return true;
    }
    const Token& previous = tokens[index - 1];
    return previous.is_punctuator(";") || previous.is_punctuator("{") ||
           previous.is_punctuator("}") || previous.is_punctuator(":");
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

void report(diagnostics::Engine& engine,
            const TokenStream& stream,
            const Token& token,
            diagnostics::Category category,
            std::string message,
            std::string note = {}) {
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
bool try_law(const TokenStream& stream,
             std::size_t index,
             diagnostics::Engine& engine,
             LawDeclaration& law,
             std::size_t& next_index) {
    const std::vector<Token>& tokens = stream.tokens();

    if (index + 2 >= tokens.size()) {
        return false;
    }
    if (tokens[index + 1].kind != TokenKind::Identifier ||
        !tokens[index + 2].is_punctuator("(")) {
        return false;
    }

    const std::size_t close = matching_parenthesis(tokens, index + 2);
    if (close >= tokens.size()) {
        return false;
    }

    std::size_t cursor = close + 1;
    if (cursor >= tokens.size() || !clause_kind(tokens[cursor]).has_value()) {
        return false;  // still ordinary C++
    }

    law.name = std::string(tokens[index + 1].text);
    law.range.begin = stream.location_of(tokens[index + 1]);
    law.keyword_location = stream.location_of(tokens[index]);
    law.parameters = source::ByteSpan{tokens[index + 2].span.end(),
                                      tokens[close].span.offset - tokens[index + 2].span.end()};

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
        clause.expression =
            source::ByteSpan{tokens[cursor + 1].span.end(),
                             tokens[clause_close].span.offset - tokens[cursor + 1].span.end()};
        if (stream.spelling(clause.expression).find_first_not_of(" \t\r\n") ==
            std::string_view::npos) {
            report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                   "'" + std::string(tokens[cursor].text) + "' requires an expression");
            malformed = true;
        }
        law.clauses.push_back(clause);
        cursor = clause_close + 1;
    }

    if (cursor >= tokens.size() || !tokens[cursor].is_punctuator(";")) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "a law declaration ends with ';'",
               "a law states a proposition and has no body");
        next_index = cursor;
        return true;
    }

    law.range.span = source::ByteSpan{tokens[index].span.offset,
                                      tokens[cursor].span.end() - tokens[index].span.offset};
    law.end_line = tokens[cursor].line;
    next_index = cursor + 1;

    const auto ensures_count = std::ranges::count_if(
        law.clauses, [](const Clause& clause) { return clause.kind == ClauseKind::Ensures; });
    if (ensures_count == 0) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "law '" + law.name + "' states no proposition",
               "a law requires exactly one ensures clause");
        malformed = true;
    } else if (ensures_count > 1) {
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "law '" + law.name + "' has " + std::to_string(ensures_count) +
                   " ensures clauses",
               "a law has exactly one ensures clause");
        malformed = true;
    }

    if (malformed) {
        law.clauses.clear();
        law.name.clear();
    }
    return true;
}

// Splits the instantiation arguments of `exact p(a, b)` at the commas that
// separate them.
//
// Only the separators are found here. Each argument is delimited, never read:
// its bytes go to Clang through the projection, which is what keeps C++
// expression meaning in one place (SPEC.md 7.3, ARCHITECTURE.md 11).
bool read_proof_arguments(const TokenStream& stream,
                          std::size_t open,
                          std::size_t close,
                          diagnostics::Engine& engine,
                          std::vector<ProofArgument>& arguments) {
    const std::vector<Token>& tokens = stream.tokens();

    std::size_t depth = 0;
    std::size_t begin = open + 1;
    for (std::size_t index = open + 1; index <= close; ++index) {
        const Token& token = tokens[index];
        const bool separator =
            depth == 0 && (index == close || token.is_punctuator(","));

        if (!separator) {
            if (token.is_punctuator("(") || token.is_punctuator("[") ||
                token.is_punctuator("{")) {
                ++depth;
            } else if (token.is_punctuator(")") || token.is_punctuator("]") ||
                       token.is_punctuator("}")) {
                if (depth == 0) {
                    report(engine, stream, token, diagnostics::Category::CpplSyntax,
                           "'" + std::string(token.text) + "' closes nothing in this "
                           "instantiation argument list");
                    return false;
                }
                --depth;
            }
            continue;
        }

        if (index == begin) {
            if (index == close && arguments.empty()) {
                return true;  // `p()` instantiates at nothing, like `p`
            }
            report(engine, stream, token, diagnostics::Category::CpplSyntax,
                   "an instantiation argument is missing",
                   "each argument of a proof reference is an ordinary C++ expression");
            return false;
        }

        arguments.push_back(ProofArgument{
            source::ByteSpan{tokens[begin].span.offset,
                             tokens[index - 1].span.end() - tokens[begin].span.offset},
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
bool read_proof_statements(const TokenStream& stream,
                           std::size_t body_open,
                           std::size_t body_close,
                           diagnostics::Engine& engine,
                           std::vector<ProofStatement>& statements) {
    const std::vector<Token>& tokens = stream.tokens();

    std::size_t cursor = body_open + 1;
    while (cursor < body_close) {
        const Token& token = tokens[cursor];

        if (token.is_identifier("refl") && cursor + 1 < body_close &&
            tokens[cursor + 1].is_punctuator(";")) {
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
            tokens[cursor + 1].kind == TokenKind::Identifier &&
            tokens[cursor + 2].is_punctuator(":")) {
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

            if (terminator >= body_close || !tokens[terminator].is_punctuator(";") ||
                terminator == cursor + 3) {
                report(engine, stream, tokens[cursor], diagnostics::Category::CpplSyntax,
                       "'assume' names a proposition, as in 'assume h : a == b;'");
                return false;
            }

            ProofStatement statement;
            statement.kind = ProofStatementKind::Assume;
            statement.reference = std::string(tokens[cursor + 1].text);
            statement.proposition = source::ByteSpan{
                tokens[cursor + 3].span.offset,
                tokens[terminator - 1].span.end() - tokens[cursor + 3].span.offset};
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
            statement.kind = is_exact      ? ProofStatementKind::Exact
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
                    report(engine, stream, tokens[cursor + 2],
                           diagnostics::Category::CpplSyntax,
                           "unterminated instantiation argument list");
                    return false;
                }
                if (!read_proof_arguments(stream, cursor + 2, close, engine,
                                          statement.arguments)) {
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
               "'" + std::string(token.text) + "' does not begin a proof statement this "
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
bool try_proof(const TokenStream& stream,
               std::size_t index,
               diagnostics::Engine& engine,
               ProofDeclaration& proof,
               std::size_t& next_index) {
    const std::vector<Token>& tokens = stream.tokens();

    if (index + 2 >= tokens.size()) {
        return false;
    }
    if (tokens[index + 1].kind != TokenKind::Identifier ||
        !tokens[index + 2].is_punctuator("(")) {
        return false;
    }

    const std::size_t close = matching_parenthesis(tokens, index + 2);
    if (close >= tokens.size() || close + 1 >= tokens.size() ||
        !tokens[close + 1].is_identifier("proves")) {
        return false;  // still ordinary C++
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
        report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
               "a proof declaration has a body",
               "a proof constructs evidence, so it ends with '{ ... }' rather than ';'");
        next_index = proves_close + 1;
        return true;
    }

    const std::size_t body_open = proves_close + 1;
    const std::size_t body_close = matching_brace(tokens, body_open);
    if (body_close >= tokens.size()) {
        report(engine, stream, tokens[body_open], diagnostics::Category::CpplSyntax,
               "unterminated proof body");
        next_index = body_open + 1;
        return true;
    }

    next_index = body_close + 1;

    proof.name = std::string(tokens[index + 1].text);
    proof.range.begin = stream.location_of(tokens[index + 1]);
    proof.range.span = source::ByteSpan{tokens[index].span.offset,
                                        tokens[body_close].span.end() - tokens[index].span.offset};
    proof.keyword_location = stream.location_of(tokens[index]);
    proof.end_line = tokens[body_close].line;
    proof.parameters = source::ByteSpan{tokens[index + 2].span.end(),
                                        tokens[close].span.offset - tokens[index + 2].span.end()};
    proof.proposition =
        source::ByteSpan{tokens[proves + 1].span.end(),
                         tokens[proves_close].span.offset - tokens[proves + 1].span.end()};
    proof.proposition_location = stream.location_of(tokens[proves]);

    bool malformed = stream.spelling(proof.proposition).find_first_not_of(" \t\r\n") ==
                     std::string_view::npos;
    if (malformed) {
        report(engine, stream, tokens[proves], diagnostics::Category::CpplSyntax,
               "'proves' requires a proposition");
    }

    if (!read_proof_statements(stream, body_open, body_close, engine, proof.statements)) {
        malformed = true;
    } else if (proof.statements.empty()) {
        report(engine, stream, tokens[index], diagnostics::Category::ProofFailure,
               "proof '" + proof.name + "' has an empty body",
               "a proof body must close the goal it states");
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
    return after.kind == TokenKind::Identifier || after.is_punctuator("::") ||
           after.is_punctuator("<") || after.is_punctuator("*") || after.is_punctuator("&") ||
           after.is_punctuator("&&");
}

std::optional<std::size_t> find_declarator_name(const std::vector<Token>& tokens,
                                                std::size_t index) {
    std::size_t depth = 0;
    for (std::size_t cursor = index + 1; cursor < tokens.size(); ++cursor) {
        const Token& token = tokens[cursor];
        if (token.kind == TokenKind::EndOfFile) {
            break;
        }
        if (token.is_punctuator("(")) {
            if (depth == 0 && cursor > index + 1 &&
                tokens[cursor - 1].kind == TokenKind::Identifier &&
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
bool has_specification_clause(const std::vector<Token>& tokens,
                              std::size_t name_index,
                              std::size_t& clause_index) {
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
        if (token.kind == TokenKind::EndOfFile || token.is_punctuator(";") ||
            token.is_punctuator("{")) {
            return false;
        }
        if (is_specification_clause(token) && cursor + 1 < tokens.size() &&
            tokens[cursor + 1].is_punctuator("(")) {
            clause_index = cursor;
            return true;
        }
    }
    return false;
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
        if (token.is_identifier("class") || token.is_identifier("struct") ||
            token.is_identifier("union") || token.is_identifier("enum")) {
            return ScopeKind::Class;
        }
        if (token.is_punctuator(")")) {
            return ScopeKind::Block;
        }
    }
    return ScopeKind::Block;
}

}  // namespace

std::string describe(ClauseKind kind) {
    switch (kind) {
        case ClauseKind::Ensures:
            return "ensures";
        case ClauseKind::Expects:
            return "expects";
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

Syntax recognize(const TokenStream& stream, diagnostics::Engine& engine) {
    const std::vector<Token>& tokens = stream.tokens();
    Syntax syntax;

    std::vector<ScopeKind> scopes;
    const auto at_namespace_scope = [&scopes] {
        return std::ranges::all_of(scopes,
                                   [](ScopeKind kind) { return kind == ScopeKind::Namespace; });
    };

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

        if (!at_declaration_start(tokens, index)) {
            ++index;
            continue;
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
                        report(engine, stream, tokens[index],
                               diagnostics::Category::UnsupportedSemantics,
                               "law '" + law.name +
                                   "' is declared outside namespace scope",
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
                        report(engine, stream, tokens[index],
                               diagnostics::Category::UnsupportedSemantics,
                               "proof '" + proof.name + "' is declared outside namespace scope",
                               "this implementation recognizes proofs at namespace scope only");
                    }
                }
                index = next;
                continue;
            }
        }

        if (tokens[index].is_identifier("verified") &&
            specifier_introduces_declaration(tokens, index)) {
            report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
                   "the 'verified' specifier is not supported by this implementation",
                   "verified functions require contract verification, which this "
                   "implementation does not perform");
            ++index;
            continue;
        }

        if (tokens[index].is_identifier("pure") &&
            specifier_introduces_declaration(tokens, index)) {
            const std::optional<std::size_t> name = find_declarator_name(tokens, index);
            if (!name.has_value()) {
                report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
                       "the 'pure' specifier applies to a function declaration",
                       "no function declarator follows this specifier");
            } else {
                std::size_t clause_index = 0;
                if (has_specification_clause(tokens, *name, clause_index)) {
                    report(engine, stream, tokens[clause_index],
                           diagnostics::Category::UnsupportedSemantics,
                           "function specification clauses are not supported by this "
                           "implementation",
                           "state the property as a law over this function instead");
                } else if (!at_namespace_scope()) {
                    report(engine, stream, tokens[index],
                           diagnostics::Category::UnsupportedSemantics,
                           "'pure' is applied outside namespace scope",
                           "this implementation recognizes pure functions at namespace scope "
                           "only");
                } else {
                    PureMarker marker;
                    marker.keyword = tokens[index].span;
                    marker.keyword_location = stream.location_of(tokens[index]);
                    marker.function_name = std::string(tokens[*name].text);
                    marker.function_location = stream.location_of(tokens[*name]);
                    syntax.pure_markers.push_back(std::move(marker));
                }
            }
        }

        ++index;
    }

    return syntax;
}

}  // namespace cppl::frontend
