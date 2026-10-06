// Recognizing proofs and the statements and arguments of a proof body.

#include "cppl/decomposition/labels.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "recognizer_state.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::frontend {

using detail::recognizer::report;

namespace {

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

} // namespace

namespace detail::recognizer {

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
                    arm.omit_keyword = tokens[cursor].span;
                    arm.by_keyword = tokens[omitted_label_end].span;
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
            statement.reference_span = tokens[cursor + 1].span;
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
            statement.reference_span = tokens[cursor + 1].span;
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
        proof.keyword = tokens[index].span;
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

} // namespace detail::recognizer

} // namespace cppl::frontend
