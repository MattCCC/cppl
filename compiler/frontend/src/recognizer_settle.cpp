// Deciding the statements written with a C++L word once the whole unit has
// been read: claims, splits, unsafe code, ghost state and validations.

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

using detail::recognizer::admit_split_arms;
using detail::recognizer::declarator_name_text;
using detail::recognizer::find_declarator_name;
using detail::recognizer::ghost_declares;
using detail::recognizer::has_specification_clause;
using detail::recognizer::holds_a_statement;
using detail::recognizer::read_proof_statements;
using detail::recognizer::Recognizer;
using detail::recognizer::record_unchecked_clauses;
using detail::recognizer::report;
using detail::recognizer::split_claims;

void Recognizer::settle_contradictions() {
    if (!written_contradictions.empty()) {
        const std::optional<std::size_t> other = other_use("contradiction", [&](std::size_t at) {
            return in_split(at) || std::ranges::any_of(written_contradictions,
                                                       [at](const Written& written) { return written.keyword == at; });
        });

        for (const Written& written : written_contradictions) {
            const auto body = std::ranges::find_if(verified_bodies, [&written](const VerifiedBody& candidate) {
                return candidate.open < written.keyword && written.keyword < candidate.close;
            });
            if (other.has_value()) {
                if (body != verified_bodies.end()) {
                    warn_ordinary(written.keyword, *other,
                                  "this statement is ordinary C++, not a claim that the path cannot occur");
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
}

void Recognizer::settle_splits() {
    // A split on a runtime path follows the same rule, word by word: `cases x
    // {...}` is a declaration with a braced initializer wherever `cases` names a
    // type (SPEC.md 3.1, CASE-017).
    if (!written_splits.empty()) {
        const std::optional<std::size_t> other_cases =
            other_use("cases", [this](std::size_t at) { return in_split(at); });
        const std::optional<std::size_t> other_decompose =
            other_use("decompose", [this](std::size_t at) { return in_split(at); });

        for (const Written& written : written_splits) {
            const Token& keyword = tokens[written.keyword];
            const std::optional<std::size_t>& other = keyword.is_identifier("cases") ? other_cases : other_decompose;
            const auto body = std::ranges::find_if(verified_bodies, [&written](const VerifiedBody& candidate) {
                return candidate.open < written.keyword && written.keyword < candidate.close;
            });
            if (other.has_value()) {
                if (body != verified_bodies.end()) {
                    warn_ordinary(written.keyword, *other, "this statement is ordinary C++, not a case split");
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
}

void Recognizer::settle_unsafe() {
    // `unsafe` follows the same rule: a block or a declaration is C++L only in a
    // unit that uses the word for nothing else (SPEC.md 3.1). A use inside a
    // law or a proof is C++L's own.
    if (!written_unsafe_blocks.empty() || !written_unsafe_declarations.empty()) {
        const auto claimed = [&](std::size_t at) {
            return std::ranges::any_of(written_unsafe_blocks,
                                       [at](const Written& written) { return written.keyword == at; }) ||
                   std::ranges::any_of(written_unsafe_declarations,
                                       [at](const WrittenUnsafeDeclaration& written) { return written.keyword == at; });
        };
        const std::optional<std::size_t> other = other_use("unsafe", claimed);
        if (other.has_value()) {
            const auto warn = [&](std::size_t at) {
                warn_ordinary(at, *other, "this is ordinary C++, not an unsafe boundary");
            };
            // Only where an unsafe boundary could have been meant (WORD-018):
            // in a verified body, or where the C++ reading cannot be valid, as
            // with braces holding a statement, which no initializer holds, and
            // with a declaration, whose word a return type follows. `unsafe{};`
            // elsewhere is a temporary and nothing else.
            for (const Written& written : written_unsafe_blocks) {
                if (in_verified_body(written.keyword) ||
                    holds_a_statement(tokens, written.keyword + 1, written.terminator)) {
                    warn(written.keyword);
                }
            }
            for (const WrittenUnsafeDeclaration& written : written_unsafe_declarations) {
                warn(written.keyword);
            }
        } else {
            for (const Written& written : written_unsafe_blocks) {
                const Token& open = tokens[written.keyword + 1];
                const Token& close = tokens[written.terminator];
                UnsafeBlock block;
                block.keyword = tokens[written.keyword].span;
                block.location = stream.location_of(tokens[written.keyword]);
                block.body = source::ByteSpan{open.span.offset, close.span.end() - open.span.offset};
                block.nested = std::ranges::any_of(written_unsafe_blocks, [&written](const Written& outer) {
                    return outer.keyword != written.keyword && outer.keyword < written.keyword &&
                           written.keyword < outer.terminator;
                });
                block.body_open = open.span.end();
                block.body_open_line = open.line;
                block.body_open_column = open.column + 1;
                if (const auto body = std::ranges::find_if(verified_bodies,
                                                           [&written](const VerifiedBody& candidate) {
                                                               return candidate.open < written.keyword &&
                                                                      written.keyword < candidate.close;
                                                           });
                    body != verified_bodies.end()) {
                    block.function_index = body->function;
                }
                syntax.unsafe_blocks.push_back(block);
            }
            for (const WrittenUnsafeDeclaration& written : written_unsafe_declarations) {
                const Token& keyword = tokens[written.keyword];
                const std::optional<std::size_t> name = find_declarator_name(tokens, written.keyword);
                if (!name.has_value()) {
                    report(engine, stream, keyword, diagnostics::Category::CpplSyntax,
                           "the 'unsafe' specifier applies to a function declaration",
                           "no function declarator follows this specifier; there is no unsafe expression form "
                           "(SPEC.md UNSAFE-002)");
                    continue;
                }
                if (!written.namespace_scope) {
                    report(engine, stream, keyword, diagnostics::Category::UnsupportedSemantics,
                           "'unsafe' is applied outside namespace scope",
                           "this implementation recognizes unsafe functions at namespace scope only");
                    continue;
                }
                // Nothing checks an unsafe function, so a contract on one would
                // be a fact its callers rest on that no proof and no trusted
                // law states (UNSAFE-003, UNSAFE-004).
                if (std::size_t clause_index = 0; has_specification_clause(tokens, *name, clause_index)) {
                    report(engine, stream, tokens[clause_index], diagnostics::Category::CpplSyntax,
                           "an unsafe function states no contract",
                           "nothing checks an unsafe function, so its callers could not rely on this clause; verify "
                           "the function, or state the property as a trusted law (SPEC.md UNSAFE-003, UNSAFE-004)");
                    record_unchecked_clauses(stream, written.keyword, *name, engine, syntax);
                    continue;
                }
                UnsafeFunction function;
                function.keyword = keyword.span;
                function.keyword_location = stream.location_of(keyword);
                function.function_name = declarator_name_text(tokens, *name);
                function.function_location = stream.location_of(tokens[*name]);
                function.function_offset = tokens[*name].span.offset;
                syntax.unsafe_functions.push_back(std::move(function));
            }
        }
    }
}

void Recognizer::settle_ghosts() {
    // `ghost` follows the same rule (SPEC.md 3.1). A declaration is ghost state
    // only as a local of a verified body, directly in a block, where it can
    // leave the program without leaving a statement's body empty (SPEC.md 25).
    if (!written_ghosts.empty()) {
        const auto claimed = [&](std::size_t at) {
            return std::ranges::any_of(written_ghosts,
                                       [at](const WrittenGhost& written) { return written.keyword == at; });
        };
        const std::optional<std::size_t> other = other_use("ghost", claimed);
        for (const WrittenGhost& written : written_ghosts) {
            const Token& keyword = tokens[written.keyword];
            if (other.has_value()) {
                // Ghost state exists only in a verified body, so only there could
                // it have been meant (WORD-018).
                if (!in_verified_body(written.keyword)) {
                    continue;
                }
                warn_ordinary(written.keyword, *other, "this is ordinary C++, not ghost state");
                continue;
            }
            const auto body = std::ranges::find_if(verified_bodies, [&written](const VerifiedBody& candidate) {
                return candidate.open < written.keyword && written.keyword < candidate.close;
            });
            if (!written.in_block) {
                report(engine, stream, keyword, diagnostics::Category::CpplSyntax,
                       "ghost state is declared only as a local of a verified body",
                       "there are no ghost globals, members or parameters (SPEC.md 25)");
                continue;
            }
            if (body == verified_bodies.end()) {
                report(engine, stream, keyword, diagnostics::Category::UnsupportedSemantics,
                       "ghost state outside a verified body would not be checked",
                       "mark the enclosing function 'verified'; ghost state exists only for its proof (SPEC.md 25)");
                continue;
            }
            const Token& previous = tokens[written.keyword - 1];
            if (!previous.is_punctuator("{") && !previous.is_punctuator("}") && !previous.is_punctuator(";")) {
                report(engine, stream, keyword, diagnostics::Category::CpplSyntax,
                       "a ghost declaration stands directly in a block",
                       "as the body of a statement or after a label it would leave that statement without one "
                       "when it is erased; write it inside braces");
                continue;
            }
            if (!ghost_declares(tokens, written.keyword, written.terminator)) {
                report(engine, stream, keyword, diagnostics::Category::CpplSyntax,
                       "a ghost declaration names a type and a variable",
                       "without a type, what follows 'ghost' is not a declaration (GRAMMAR.md 21)");
                continue;
            }
            GhostDeclaration ghost;
            ghost.function_index = body->function;
            ghost.keyword = keyword.span;
            ghost.location = stream.location_of(keyword);
            ghost.erased =
                source::ByteSpan{keyword.span.offset, tokens[written.terminator].span.end() - keyword.span.offset};
            syntax.ghost_declarations.push_back(ghost);
        }
    }
}

void Recognizer::refuse_proof_syntax_in_unsafe() {
    for (const GhostDeclaration& ghost : syntax.ghost_declarations) {
        if (inside_unsafe(ghost.keyword.offset)) {
            report(engine, ghost.location, diagnostics::Category::UnsupportedSemantics,
                   "ghost state inside an unsafe block would not be checked",
                   "an unsafe block's statements are not a path the verifier walks, so declare it outside the block");
        }
    }
    for (const LoopSpecification& loop : syntax.loops) {
        if (inside_unsafe(loop.keyword.offset)) {
            report(engine, loop.keyword_location, diagnostics::Category::UnsupportedSemantics,
                   "a loop specification inside an unsafe block would not be checked",
                   "an unsafe block's statements are not verified, so move the loop out of it or drop its clauses");
        }
    }
    for (const PathContradiction& claim : syntax.path_contradictions) {
        if (!claim.split.has_value() && inside_unsafe(claim.span.offset)) {
            report(engine, claim.statement.location, diagnostics::Category::UnsupportedSemantics,
                   "a claim that a path cannot occur inside an unsafe block would not be checked",
                   "an unsafe block's statements are not a path the verifier walks");
        }
    }
    for (const PathCaseSplit& split : syntax.path_splits) {
        if (inside_unsafe(split.span.offset)) {
            report(engine, split.statement.location, diagnostics::Category::UnsupportedSemantics,
                   "a case split inside an unsafe block would not be checked",
                   "an unsafe block's statements are not a path the verifier walks");
        }
    }
}

void Recognizer::read_validations() {
    // A validation expression tests a value against a refinement's predicate at
    // run time (SPEC.md 28.1). Every expression spelled `validate<...>(` is
    // found here, over the whole unit, whatever statement or clause holds it:
    // the template argument list closes at the `>` that balances the `<`,
    // within the statement, and a `(` must follow it. C++ comes first: where
    // the unit uses `validate` for anything but validations, every one of them
    // is ordinary C++ (WORD-013). Otherwise each must stand in a verified body,
    // outside every loop clause and unsafe block, and name exactly one
    // refinement type the unit declares.
    std::vector<Written> written_validations;
    for (std::size_t at = 0; at + 1 < tokens.size(); ++at) {
        if (!tokens[at].is_identifier("validate") || !tokens[at + 1].is_punctuator("<")) {
            continue;
        }
        std::size_t depth = 0;
        std::size_t close = at + 1;
        for (; close < tokens.size(); ++close) {
            if (tokens[close].is_punctuator("<")) {
                ++depth;
            } else if (tokens[close].is_punctuator(">")) {
                if (--depth == 0) {
                    break;
                }
            } else if (tokens[close].is_punctuator(">>")) {
                depth = depth >= 2 ? depth - 2 : 0;
                if (depth == 0) {
                    break;
                }
            } else if (tokens[close].is_punctuator(";") || tokens[close].is_punctuator("{") ||
                       tokens[close].is_punctuator("}") || tokens[close].kind == TokenKind::EndOfFile) {
                close = tokens.size();
                break;
            }
        }
        if (close + 1 < tokens.size() && tokens[close + 1].is_punctuator("(")) {
            written_validations.push_back(Written{at, close});
        }
    }
    const auto in_loop_clause = [this](std::size_t offset) {
        return std::ranges::any_of(syntax.loops, [offset](const LoopSpecification& loop) {
            return offset >= loop.clause_region.offset && offset < loop.clause_region.end();
        });
    };
    if (!written_validations.empty()) {
        const std::optional<std::size_t> other = other_use("validate", [&](std::size_t at) {
            return std::ranges::any_of(written_validations,
                                       [at](const Written& written) { return written.keyword == at; });
        });
        for (const Written& written : written_validations) {
            const Token& keyword = tokens[written.keyword];
            const auto body = std::ranges::find_if(verified_bodies, [&written](const VerifiedBody& candidate) {
                return candidate.open < written.keyword && written.keyword < candidate.close;
            });
            if (other.has_value()) {
                if (body != verified_bodies.end()) {
                    warn_ordinary(written.keyword, *other, "this expression is ordinary C++, not a validation");
                }
                continue;
            }
            if (body == verified_bodies.end()) {
                report(engine, stream, keyword, diagnostics::Category::UnsupportedSemantics,
                       "a validation expression is checked only in the body of a verified function",
                       "a validation is runtime code: test the value in a verified body, not in a declaration, "
                       "a contract or a function that is not verified (SPEC.md RUNTIMECHECK-019)");
                continue;
            }
            if (in_loop_clause(keyword.span.offset)) {
                report(engine, stream, keyword, diagnostics::Category::UnsupportedSemantics,
                       "a loop clause states a proposition, and a validation expression is runtime code",
                       "test the value in the loop's condition or body (SPEC.md RUNTIMECHECK-019)");
                continue;
            }
            if (inside_unsafe(keyword.span.offset)) {
                report(engine, stream, keyword, diagnostics::Category::UnsupportedSemantics,
                       "a validation expression inside an unsafe block would not be checked",
                       "an unsafe block's statements are not a path the verifier walks");
                continue;
            }
            if (written.terminator != written.keyword + 3 ||
                tokens[written.keyword + 2].kind != TokenKind::Identifier) {
                report(engine, stream, keyword, diagnostics::Category::UnsupportedSemantics,
                       "a validation names the refinement type it tests by the name its declaration gives it",
                       "write validate<R>(value) with R the refinement type's own name (SPEC.md RUNTIMECHECK-018)");
                continue;
            }
            const std::string name{tokens[written.keyword + 2].text};
            std::vector<std::size_t> named;
            for (std::size_t refinement = 0; refinement < syntax.refinement_types.size(); ++refinement) {
                if (syntax.refinement_types[refinement].name == name) {
                    named.push_back(refinement);
                }
            }
            if (named.empty()) {
                report(engine, stream, keyword, diagnostics::Category::UnsupportedSemantics,
                       "'" + name + "' does not name a refinement type this translation unit declares",
                       "a validation tests a value against a refinement's predicate (SPEC.md RUNTIMECHECK-018)");
                continue;
            }
            if (named.size() > 1) {
                report(engine, stream, keyword, diagnostics::Category::UnsupportedSemantics,
                       "this translation unit declares more than one refinement type named '" + name + "'",
                       "a validation must name exactly one refinement type");
                continue;
            }
            RefinementType& refinement = syntax.refinement_types[named.front()];
            if (refinement.indexed) {
                report(engine, stream, keyword, diagnostics::Category::UnsupportedSemantics,
                       "validating a value against the indexed refinement type '" + name + "' is not supported",
                       "validate against a refinement type without indices (SPEC.md RUNTIMECHECK-020)");
                continue;
            }
            refinement.validator = "__cppl_v_" + name;
            ValidationExpression validation;
            validation.function_index = body->function;
            validation.refinement_index = named.front();
            validation.callee =
                source::ByteSpan{keyword.span.offset, tokens[written.terminator].span.end() - keyword.span.offset};
            validation.location = stream.location_of(keyword);
            syntax.validations.push_back(validation);
        }
    }
}

} // namespace cppl::frontend
