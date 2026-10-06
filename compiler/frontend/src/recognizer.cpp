#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "recognition.hpp"
#include "recognizer_state.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::frontend {

using detail::recognizer::at_declaration_start;
using detail::recognizer::first_module_import;
using detail::recognizer::Recognizer;

namespace {

constexpr std::array<std::string_view, 23> kTypeKeywords = {
    "void",     "bool",     "char",   "char8_t", "char16_t", "char32_t", "wchar_t",  "short",
    "int",      "long",     "float",  "double",  "signed",   "unsigned", "auto",     "const",
    "volatile", "typename", "struct", "class",   "enum",     "decltype", "constexpr"};

} // namespace

namespace detail::recognizer {

bool is_type_keyword(const Token& token) {
    return token.kind == TokenKind::Identifier && std::ranges::find(kTypeKeywords, token.text) != kTypeKeywords.end();
}

} // namespace detail::recognizer

namespace {

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

} // namespace

namespace detail::recognizer {

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

} // namespace detail::recognizer

namespace {

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

} // namespace

namespace detail::recognizer {

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

// A span from the first byte of `first` through the last byte of `last`.
source::ByteSpan tokens_span(const Token& first, const Token& last) {
    return source::ByteSpan{first.span.offset, last.span.end() - first.span.offset};
}

} // namespace detail::recognizer

std::vector<MeasureComponent> measure_components(const TokenStream& stream, const Clause& clause) {
    const std::vector<Token>& tokens = stream.tokens();
    const auto first = std::ranges::lower_bound(tokens, clause.expression.offset, {},
                                                [](const Token& token) { return token.span.offset; });
    std::vector<MeasureComponent> components;
    std::optional<std::size_t> start;
    std::size_t end = clause.expression.offset;
    std::size_t depth = 0;
    const auto close = [&] {
        MeasureComponent component;
        if (start.has_value()) {
            component.expression = source::ByteSpan{tokens[*start].span.offset, end - tokens[*start].span.offset};
            component.location = stream.location_of(tokens[*start]);
        }
        components.push_back(component);
        start.reset();
    };
    for (auto token = first; token != tokens.end() && token->span.end() <= clause.expression.end(); ++token) {
        if (token->kind == TokenKind::EndOfFile) {
            break;
        }
        if (depth == 0 && token->is_punctuator(",")) {
            close();
            continue;
        }
        if (token->is_punctuator("(") || token->is_punctuator("[") || token->is_punctuator("{")) {
            ++depth;
        } else if ((token->is_punctuator(")") || token->is_punctuator("]") || token->is_punctuator("}")) && depth > 0) {
            --depth;
        }
        if (!start.has_value()) {
            start = static_cast<std::size_t>(token - tokens.begin());
        }
        end = token->span.end();
    }
    close();
    return components;
}

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

namespace {

std::optional<source::SourceLocation> unread_in(const ProofStatement& statement) {
    if (statement.kind == ProofStatementKind::Unread) {
        return statement.location;
    }
    for (const ProofArm& arm : statement.arms) {
        for (const ProofStatement& inner : arm.statements) {
            if (std::optional<source::SourceLocation> unread = unread_in(inner)) {
                return unread;
            }
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<source::SourceLocation> draft_only(const Syntax& syntax) {
    for (const LawDeclaration& law : syntax.laws) {
        if (law.completeness != Completeness::Whole) {
            return law.keyword_location;
        }
    }
    for (const ProofDeclaration& proof : syntax.proofs) {
        if (proof.completeness != Completeness::Whole) {
            return proof.keyword_location;
        }
        for (const ProofStatement& statement : proof.statements) {
            if (std::optional<source::SourceLocation> unread = unread_in(statement)) {
                return unread;
            }
        }
    }
    for (const PathContradiction& claim : syntax.path_contradictions) {
        if (std::optional<source::SourceLocation> unread = unread_in(claim.statement)) {
            return unread;
        }
    }
    for (const PathCaseSplit& split : syntax.path_splits) {
        if (std::optional<source::SourceLocation> unread = unread_in(split.statement)) {
            return unread;
        }
    }
    return std::nullopt;
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

const Clause* VerifiedFunction::measure() const {
    for (const Clause& clause : clauses) {
        if (clause.kind == ClauseKind::Decreases) {
            return &clause;
        }
    }
    return nullptr;
}

Syntax recognize(const TokenStream& stream, diagnostics::Engine& engine, RecognitionMode mode) {
    Recognizer recognizer(stream, engine, mode);
    recognizer.read_declarations();
    recognizer.find_module_import();
    recognizer.settle_contradictions();
    recognizer.settle_splits();
    recognizer.settle_unsafe();
    recognizer.settle_ghosts();
    recognizer.refuse_proof_syntax_in_unsafe();
    recognizer.read_validations();
    return std::move(recognizer.syntax);
}

void Recognizer::warn_ordinary(std::size_t at, std::size_t other, std::string_view rest) {
    const bool imported = !tokens[other].is_identifier(tokens[at].text);
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = diagnostics::Severity::Warning;
    diagnostic.category = diagnostics::Category::CpplSyntax;
    diagnostic.message = "'" + std::string(tokens[at].text) +
                         (imported ? "' may name an entity of a module this translation unit imports, so "
                                   : "' is also a name in this translation unit, so ") +
                         std::string(rest);
    diagnostic.location = stream.location_of(tokens[at]);
    diagnostic.notes.push_back(diagnostics::Note{imported ? "the module is imported here" : "the name is used here",
                                                 stream.location_of(tokens[other])});
    engine.report(std::move(diagnostic));
}

void Recognizer::find_module_import() {
    module_import = first_module_import(tokens);
}

} // namespace cppl::frontend
