// The recognizer's one pass over the tokens: every declaration and statement
// it reads.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "recognizer_state.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::frontend {

using detail::recognizer::at_declaration_start;
using detail::recognizer::at_statement_start;
using detail::recognizer::class_name_before;
using detail::recognizer::contradiction_statement_end;
using detail::recognizer::declarator_name_text;
using detail::recognizer::find_declarator_name;
using detail::recognizer::ghost_declaration_end;
using detail::recognizer::has_cppl_keyword;
using detail::recognizer::has_specification_clause;
using detail::recognizer::is_one_of;
using detail::recognizer::is_specifier;
using detail::recognizer::kSpecifiersAfterType;
using detail::recognizer::LoopClauses;
using detail::recognizer::matching_brace;
using detail::recognizer::matching_parenthesis;
using detail::recognizer::Recognizer;
using detail::recognizer::record_unchecked_clauses;
using detail::recognizer::refuse_lifetime_member;
using detail::recognizer::report;
using detail::recognizer::scope_kind_before;
using detail::recognizer::ScopeKind;
using detail::recognizer::specifier_introduces_declaration;
using detail::recognizer::specifiers_start;
using detail::recognizer::split_statement_end;
using detail::recognizer::try_explicit_instantiation;
using detail::recognizer::try_law;
using detail::recognizer::try_loop_clauses;
using detail::recognizer::try_proof;
using detail::recognizer::try_refinement_type;
using detail::recognizer::try_verified;
using detail::recognizer::written_between;

void Recognizer::read_declarations() {
    std::size_t index = 0;
    while (index < tokens.size() && tokens[index].kind != TokenKind::EndOfFile) {
        if (tokens[index].is_punctuator("{")) {
            scopes.push_back(scope_kind_before(tokens, index));
            scope_names.push_back(scopes.back() == ScopeKind::Class ? class_name_before(tokens, index)
                                                                    : std::string_view{});
            ++index;
            continue;
        }
        if (tokens[index].is_punctuator("}")) {
            if (!scopes.empty()) {
                scopes.pop_back();
                scope_names.pop_back();
            }
            ++index;
            continue;
        }

        // `verified` before a constructor or a destructor of the class it
        // stands in, after any ordinary specifiers. Neither declarator has a
        // return type, so the specifier is not otherwise read as introducing a
        // declaration; it is refused here rather than left for Clang to report
        // as an unknown type.
        if (!tolerant && tokens[index].is_identifier("verified") && at_member_scope() &&
            at_declaration_start(tokens, index) && !scope_names.back().empty()) {
            const std::string_view class_name = scope_names.back();
            std::size_t declarator = index + 1;
            while (declarator < tokens.size() && (is_one_of(tokens[declarator], kSpecifiersAfterType) ||
                                                  tokens[declarator].is_identifier("explicit"))) {
                ++declarator;
            }
            const bool constructor = declarator + 1 < tokens.size() && tokens[declarator].is_identifier(class_name) &&
                                     tokens[declarator + 1].is_punctuator("(");
            const bool destructor = declarator + 2 < tokens.size() && tokens[declarator].is_punctuator("~") &&
                                    tokens[declarator + 1].is_identifier(class_name) &&
                                    tokens[declarator + 2].is_punctuator("(");
            if (constructor || destructor) {
                refuse_lifetime_member(stream, tokens[index], destructor, engine);
                ++index;
                continue;
            }
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

        // unsafe compound-statement  (GRAMMAR.md 22). The block's statements
        // are ordinary C++ and are read on as usual, so the scan goes on into it.
        if (tokens[index].is_identifier("unsafe") && index + 1 < tokens.size() &&
            tokens[index + 1].is_punctuator("{") && !scopes.empty() && scopes.back() == ScopeKind::Block &&
            at_statement_start(tokens, index)) {
            if (const std::size_t close = matching_brace(tokens, index + 1); close < tokens.size()) {
                written_unsafe_blocks.push_back(Written{index, close});
            }
            ++index;
            continue;
        }

        // ghost simple-declaration  (GRAMMAR.md 21, SPEC.md 25).
        if (tokens[index].is_identifier("ghost")) {
            const bool in_block =
                !scopes.empty() && scopes.back() == ScopeKind::Block && at_statement_start(tokens, index);
            if (in_block || at_declaration_start(tokens, index)) {
                if (const std::optional<std::size_t> end = ghost_declaration_end(tokens, index)) {
                    written_ghosts.push_back(WrittenGhost{index, *end, in_block});
                    index = *end + 1;
                    continue;
                }
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
                        law.trusted_keyword = tokens[index].span;
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
                law.trusted_keyword = tokens[index].span;
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

        // unsafe T f(parameters);  (GRAMMAR.md 23, SPEC.md UNSAFE-001). Whether
        // this is C++L is decided once the unit has been read. `unsafe` never
        // waives what `verified` or `pure` asks for (UNSAFE-002), so it is not
        // combined with either.
        if (tokens[index].is_identifier("unsafe") && specifier_introduces_declaration(tokens, index)) {
            if (is_specifier(tokens, index + 1, "verified") || is_specifier(tokens, index + 1, "pure")) {
                report(engine, stream, tokens[index], diagnostics::Category::CpplSyntax,
                       "'unsafe' cannot be combined with '" + std::string(tokens[index + 1].text) + "'",
                       "an unsafe function is not verified: it marks a boundary whose safety is not established, so "
                       "it cannot also claim what '" +
                           std::string(tokens[index + 1].text) + "' asks to be checked (SPEC.md UNSAFE-002)");
                index += 2;
                continue;
            }
            written_unsafe_declarations.push_back(WrittenUnsafeDeclaration{index, at_namespace_scope()});
            ++index;
            continue;
        }

        if (tokens[index].is_identifier("verified") && specifier_introduces_declaration(tokens, index)) {
            VerifiedFunction verified;
            std::size_t next = index + 1;
            // A member function of a class that stands at namespace scope, or
            // in another such class, is verified with its implicit object
            // (SPEC.md CLASS-008). A class local to a function body is not:
            // its members are not declarations the trust report can name.
            const bool member = at_member_scope();
            if (try_verified(stream, index, engine, verified, next, member, !tolerant,
                             member ? scope_names.back() : std::string_view{})) {
                if (!at_namespace_scope() && !member) {
                    report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
                           "'verified' is applied outside namespace scope",
                           "this implementation verifies functions at namespace scope and member functions of "
                           "classes declared there");
                }
                if (at_namespace_scope() || member || (tolerant && at_layout_scope())) {
                    // `verified pure` is both: the contract is discharged here,
                    // and the function is still a candidate definition for the
                    // formal core.
                    if (is_specifier(tokens, index + 1, "pure")) {
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
            if (is_specifier(tokens, index + 1, "unsafe")) {
                report(engine, stream, tokens[index + 1], diagnostics::Category::CpplSyntax,
                       "'unsafe' cannot be combined with 'pure'",
                       "an unsafe function is not verified: it marks a boundary whose safety is not established, so "
                       "it cannot also claim what 'pure' asks to be checked (SPEC.md UNSAFE-002)");
                index += 2;
                continue;
            }
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
                } else if (!at_namespace_scope() &&
                           !(at_member_scope() &&
                             written_between(tokens, specifiers_start(tokens, index), *name, "static"))) {
                    // A static member function has no implicit object: it is a
                    // function, and pure as one is (SPEC.md CLASS-012). A member
                    // function with an implicit object is not a definition of its
                    // arguments alone.
                    report(engine, stream, tokens[index], diagnostics::Category::UnsupportedSemantics,
                           "'pure' is applied outside namespace scope",
                           "this implementation recognizes pure functions at namespace scope, and static member "
                           "functions of classes declared there");
                } else {
                    PureMarker marker;
                    marker.keyword = tokens[index].span;
                    marker.keyword_location = stream.location_of(tokens[index]);
                    marker.function_name = declarator_name_text(tokens, *name);
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
}

} // namespace cppl::frontend
