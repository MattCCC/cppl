#include "cppl/formatter/format.hpp"

#include "cppl/driver/process.hpp"
#include "cppl/driver/scratch.hpp"
#include "cppl/frontend/token.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <unordered_map>

#ifndef CPPL_DEFAULT_CLANG_FORMAT
#define CPPL_DEFAULT_CLANG_FORMAT "clang-format"
#endif

#ifndef CPPL_REPO_CLANG_FORMAT_CONFIG
#define CPPL_REPO_CLANG_FORMAT_CONFIG ""
#endif

namespace cppl::formatter {

namespace {

void report(std::vector<diagnostics::Diagnostic>& out, diagnostics::Severity severity, diagnostics::Category category,
            std::string message) {
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.category = category;
    diagnostic.message = std::move(message);
    out.push_back(std::move(diagnostic));
}

std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

// A declaration or loop header's clause block: the span to replace, the
// clauses to re-emit inside it in source order, and the column the whole
// declaration/loop starts at (clauses are indented one level past it, per the
// canonical rule).
struct ClauseRegion {
    source::ByteSpan span;
    std::vector<const frontend::Clause*> clauses;
    std::size_t declaration_column = 0;
    std::size_t declaration_offset = 0; // the declaration's own first byte, e.g. 'law'/'verified'
};

// The 0-based column (in bytes, not UTF-16 - indentation is always ASCII
// whitespace) of the first character of the line containing `offset`.
std::size_t line_start_column(std::string_view text, std::size_t offset) {
    std::size_t line_start = text.rfind('\n', offset == 0 ? 0 : offset - 1);
    line_start = (line_start == std::string_view::npos) ? 0 : line_start + 1;
    return offset - line_start;
}

// The 1-based line number containing `offset`.
std::size_t line_number(std::string_view text, std::size_t offset) {
    return static_cast<std::size_t>(
               std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), '\n')) +
           1;
}

constexpr std::size_t kIndentWidth = 4; // .clang-format: IndentWidth 4

std::string_view keyword_spelling(frontend::ClauseKind kind) {
    switch (kind) {
        case frontend::ClauseKind::Decreases:
            return "decreases";
        case frontend::ClauseKind::Proves:
            return "proves";
        case frontend::ClauseKind::Ensures:
            return "ensures";
        case frontend::ClauseKind::Expects:
            return "expects";
        case frontend::ClauseKind::Invariant:
            return "invariant";
    }
    return "";
}

// A predicate span's already-clang-formatted text, looked up by its original
// offset. Populated once per `format_ranges_once` call from a single batched
// clang-format invocation (`format_expression_spans`) over every clause/
// proof/refinement predicate in the document, so this layer relocates
// clauses and reads back Clang's own expression formatting rather than
// reimplementing operator spacing (AGENTS.md 14).
using PredicateText = std::unordered_map<std::size_t, std::string>;

// Whether a predicate contains syntax that exists only in C++L, and so cannot
// be handed to clang-format.
//
// clang-format is a C++ tool. It lexes the implication `->` as member access
// and glues it to its operands (`x == 1u -> x <= 1u` becomes `x == 1u->x`),
// and it reads a quantifier as a call followed by a braced initializer list,
// deleting the spacing SPEC.md 8 writes (`forall (T x) { P }` becomes
// `forall(T x){P}`). Both rewrites change what the predicate means, so a
// predicate spelling either construct keeps its source text and this layer
// does not claim Clang's authority (AGENTS.md 14) over syntax Clang does not
// have. Ordinary C++ predicates are unaffected and still go through Clang.
bool has_cppl_only_syntax(std::string_view predicate) {
    if (predicate.find("->") != std::string_view::npos) {
        return true;
    }

    // `x * y` opening a predicate is a multiplication, but clang-format sees
    // the enclosing `law`/`proves` as a declaration context and repunctuates
    // it as the pointer declaration `x* y`. A literal operand cannot begin a
    // declaration, so only an identifier-times-identifier prefix is at risk.
    const std::size_t first = predicate.find_first_not_of(" \t\r\n");
    if (first != std::string_view::npos &&
        (std::isalpha(static_cast<unsigned char>(predicate[first])) || predicate[first] == '_')) {
        std::size_t after = first;
        while (after < predicate.size() &&
               (std::isalnum(static_cast<unsigned char>(predicate[after])) || predicate[after] == '_')) {
            ++after;
        }
        const std::size_t operator_at = predicate.find_first_not_of(" \t\r\n", after);
        if (operator_at != std::string_view::npos && (predicate[operator_at] == '*' || predicate[operator_at] == '&') &&
            operator_at + 1 < predicate.size() && predicate[operator_at + 1] != '=' &&
            predicate[operator_at + 1] != predicate[operator_at]) {
            return true;
        }
    }
    for (std::string_view quantifier : {"forall", "exists"}) {
        for (std::size_t at = predicate.find(quantifier); at != std::string_view::npos;
             at = predicate.find(quantifier, at + 1)) {
            const bool starts_word =
                at == 0 || (!std::isalnum(static_cast<unsigned char>(predicate[at - 1])) && predicate[at - 1] != '_');
            const std::size_t after = at + quantifier.size();
            const bool ends_word =
                after >= predicate.size() ||
                (!std::isalnum(static_cast<unsigned char>(predicate[after])) && predicate[after] != '_');
            if (starts_word && ends_word) {
                return true;
            }
        }
    }
    return false;
}

std::string_view predicate_text(std::string_view text, source::ByteSpan span, const PredicateText& reformatted) {
    const auto found = reformatted.find(span.offset);
    if (found != reformatted.end()) {
        return found->second;
    }
    return text.substr(span.offset, span.length); // defensive fallback: never seen by the batch call
}

// Every '//' line comment sitting on its own line strictly between
// `begin`/`end` (the gap between one clause's ')' and the next clause
// keyword), each returned exactly as written. Only line comments are looked
// for: a clause's own predicate cannot itself span this gap (it ends at the
// ')' that bounds it), and a block comment here is rare enough in written
// contracts that, absent a test requiring it, this stays scoped to the one
// shape GRAMMAR.md's own examples and this test suite actually show.
std::vector<std::string_view> comments_between(std::string_view text, std::size_t begin, std::size_t end) {
    std::vector<std::string_view> comments;
    std::size_t cursor = begin;
    while (cursor < end) {
        const std::size_t slashes = text.find("//", cursor);
        if (slashes == std::string_view::npos || slashes >= end) {
            break;
        }
        std::size_t line_end = text.find('\n', slashes);
        if (line_end == std::string_view::npos || line_end > end) {
            line_end = end;
        }
        comments.push_back(text.substr(slashes, line_end - slashes));
        cursor = line_end;
    }
    return comments;
}

// Every clause on its own line, indented one level past the declaration,
// exactly as GRAMMAR.md's `verified`/`law`/loop-header examples show. The
// predicate text is Clang's own clang-format output for that expression
// (`reformatted`): this layer relocates clauses, it never reimplements the
// expression spacing clang-format already owns. A '//' comment written
// between two clauses in the original source (e.g. explaining the next
// clause) is preserved on its own line immediately before that clause,
// tracked by source adjacency so it survives even when canonical clause
// order differs from source order.
std::string canonical_clause_block(std::string_view text, const std::vector<const frontend::Clause*>& clauses,
                                   std::size_t declaration_column, const PredicateText& reformatted) {
    const std::string indent(declaration_column + kIndentWidth, ' ');

    // Keyed by a clause's own keyword offset: the comment lines written
    // immediately before it in the ORIGINAL source, found between the
    // previous clause (in source order) and this one.
    std::unordered_map<std::size_t, std::vector<std::string_view>> leading_comments;
    for (std::size_t i = 1; i < clauses.size(); ++i) {
        std::vector<std::string_view> found =
            comments_between(text, clauses[i - 1]->expression.end() + 1, clauses[i]->keyword.offset);
        if (!found.empty()) {
            leading_comments.emplace(clauses[i]->keyword.offset, std::move(found));
        }
    }

    std::string block;
    auto ordered = clauses;
    std::ranges::stable_sort(ordered, [](const auto* a, const auto* b) {
        const auto rank = [](frontend::ClauseKind kind) {
            if (kind == frontend::ClauseKind::Expects || kind == frontend::ClauseKind::Invariant)
                return 0;
            return kind == frontend::ClauseKind::Decreases ? 2 : 1;
        };
        return rank(a->kind) < rank(b->kind);
    });
    for (const frontend::Clause* clause : ordered) {
        if (const auto found = leading_comments.find(clause->keyword.offset); found != leading_comments.end()) {
            for (std::string_view comment : found->second) {
                block += '\n';
                block += indent;
                block += comment;
            }
        }
        block += '\n';
        block += indent;
        block += keyword_spelling(clause->kind);
        block += " (";
        block += predicate_text(text, clause->expression, reformatted);
        block += ')';
    }
    return block;
}

bool is_horizontal_or_newline(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// When `declaration_offset` is a `law`/`verified` declaration immediately
// preceded (across only whitespace) by a `template < ... >` header, the
// whitespace between the header's '>' and the declaration - i.e. GRAMMAR.md
// 39's "C++ owns template syntax" boundary. clang-format's own
// AlwaysBreakTemplateDeclarations: MultiLine style is free to join a short
// template header onto its declaration's line (verified directly), but a
// C++L declaration's own contract clauses already move it onto multiple
// lines below, so the header must stay on its own line too rather than
// crowding onto the same line as the declarator it introduces.
std::optional<source::ByteSpan> template_header_break(std::string_view text, std::size_t declaration_offset) {
    std::size_t begin = declaration_offset;
    while (begin > 0 && is_horizontal_or_newline(text[begin - 1]))
        --begin;
    if (begin == 0 || text[begin - 1] != '>') {
        return std::nullopt;
    }
    std::size_t depth = 0;
    std::size_t scan = begin - 1;
    std::size_t matched = text.size();
    while (true) {
        if (text[scan] == '>') {
            ++depth;
        } else if (text[scan] == '<') {
            --depth;
            if (depth == 0) {
                matched = scan;
                break;
            }
        }
        if (scan == 0) {
            break;
        }
        --scan;
    }
    if (matched >= text.size()) {
        return std::nullopt;
    }
    std::size_t keyword_end = matched;
    while (keyword_end > 0 && is_horizontal_or_newline(text[keyword_end - 1]))
        --keyword_end;
    constexpr std::string_view kTemplate = "template";
    if (keyword_end < kTemplate.size() || text.substr(keyword_end - kTemplate.size(), kTemplate.size()) != kTemplate) {
        return std::nullopt;
    }
    return source::ByteSpan{begin, declaration_offset - begin};
}

// Whether the '{' at `brace` opens a namespace body, mirroring the
// recognizer's own `scope_kind_before` (recognizer.cpp): scan back to the
// token that decides what this brace is, stopping at whatever already ends a
// previous declaration/statement.
bool opens_namespace_body(const std::vector<frontend::Token>& tokens, std::size_t brace) {
    for (std::size_t cursor = brace; cursor > 0; --cursor) {
        const frontend::Token& token = tokens[cursor - 1];
        if (token.is_punctuator(";") || token.is_punctuator("{") || token.is_punctuator("}")) {
            return false;
        }
        if (token.is_identifier("namespace")) {
            return true;
        }
        if (token.is_identifier("class") || token.is_identifier("struct") || token.is_identifier("union") ||
            token.is_identifier("enum") || token.is_punctuator(")")) {
            return false;
        }
    }
    return false;
}

// How many indent levels deep `offset` sits, independent of whatever column
// the raw, not-yet-canonically-indented source happens to place it at: one
// level per enclosing brace, except a namespace's own (this repo's
// convention, matched by `namespace_scope_indentation_is_respected_for_
// contract_clauses`: namespace scope does not indent its contents, class and
// block scope do - the same convention clang-format itself applies with
// NamespaceIndentation: None). A loop, member function or Law nested inside
// a class/block cannot rely on its raw source column for this reason: this
// implementation's own indentation is the one authority, the same way
// clang-format is the authority for ordinary C++ block indentation.
std::size_t layout_depth_at(const frontend::TokenStream& stream, std::size_t offset) {
    const std::vector<frontend::Token>& tokens = stream.tokens();
    std::size_t depth = 0;
    std::vector<bool> counted; // per open brace still on the stack: did it add a level?
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        if (tokens[index].span.offset >= offset) {
            break;
        }
        if (tokens[index].is_punctuator("{")) {
            const bool counts = !opens_namespace_body(tokens, index);
            if (counts) {
                ++depth;
            }
            counted.push_back(counts);
        } else if (tokens[index].is_punctuator("}") && !counted.empty()) {
            if (counted.back()) {
                --depth;
            }
            counted.pop_back();
        }
    }
    return depth;
}

// One region per law/verified-function clause block and loop invariant
// block. `where` on a refinement type is deliberately never visited: it
// stays inline (the request is explicit about this).
std::vector<ClauseRegion> collect_regions(const frontend::TokenStream& stream, const frontend::Syntax& syntax) {
    std::vector<ClauseRegion> regions;

    for (const frontend::LawDeclaration& law : syntax.laws) {
        if (law.clauses.empty()) {
            continue;
        }
        ClauseRegion region;
        region.declaration_column = layout_depth_at(stream, law.range.span.offset) * kIndentWidth;
        region.declaration_offset = law.range.span.offset;
        const source::ByteSpan first = law.clauses.front().keyword;
        const source::ByteSpan last = law.clauses.back().expression;
        region.span = source::ByteSpan{first.offset, (last.end() + 1) - first.offset};
        for (const frontend::Clause& clause : law.clauses) {
            region.clauses.push_back(&clause);
        }
        regions.push_back(std::move(region));
    }

    // `syntax.unchecked_clauses` (a 'pure' function's clause this
    // implementation does not check - see the field's own doc comment) uses
    // the identical `VerifiedFunction` clause layout, so it is folded into
    // the same scan rather than duplicating it.
    for (const std::vector<frontend::VerifiedFunction>* group :
         {&syntax.verified_functions, &syntax.unchecked_clauses}) {
        for (const frontend::VerifiedFunction& verified : *group) {
            if (verified.clauses.empty()) {
                continue;
            }
            ClauseRegion region;
            region.declaration_column = layout_depth_at(stream, verified.keyword.offset) * kIndentWidth;
            region.declaration_offset = verified.keyword.offset;
            region.span = verified.clause_region;
            for (const frontend::Clause& clause : verified.clauses) {
                region.clauses.push_back(&clause);
            }
            regions.push_back(std::move(region));
        }
    }

    for (const frontend::LoopSpecification& loop : syntax.loops) {
        if (loop.invariants.empty() && !loop.decreases) {
            continue;
        }
        ClauseRegion region;
        region.declaration_column = layout_depth_at(stream, loop.keyword.offset) * kIndentWidth;
        region.span = loop.clause_region;
        for (const frontend::Clause& clause : loop.invariants) {
            region.clauses.push_back(&clause);
        }
        if (loop.decreases.has_value()) {
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access): guarded on the
            // line above; the check's dataflow does not carry the guard here.
            region.clauses.push_back(&loop.decreases.value());
        }
        regions.push_back(std::move(region));
    }

    return regions;
}

// `proves (...)` is not a `Clause` (it is the fixed, single specification
// clause of a proof declaration, GRAMMAR.md 4), so it gets its own emission
// path rather than being folded into `canonical_clause_block`, which is
// written in terms of `Clause`.
struct ProofRegion {
    source::ByteSpan span;
    source::ByteSpan proposition;
    std::size_t declaration_column = 0;
    source::SourceLocation location; // the 'proves' keyword's presumed location
};

std::vector<ProofRegion> collect_proof_regions(const frontend::TokenStream& stream, const frontend::Syntax& syntax) {
    std::vector<ProofRegion> regions;
    for (const frontend::ProofDeclaration& proof : syntax.proofs) {
        if (proof.proves_keyword.length == 0) {
            continue;
        }
        ProofRegion region;
        region.declaration_column = layout_depth_at(stream, proof.range.span.offset) * kIndentWidth;
        region.span =
            source::ByteSpan{proof.proves_keyword.offset, (proof.proposition.end() + 1) - proof.proves_keyword.offset};
        region.proposition = proof.proposition;
        region.location = proof.proposition_location;
        regions.push_back(region);
    }
    return regions;
}

std::string canonical_proves_block(std::string_view text, const ProofRegion& region, const PredicateText& reformatted) {
    const std::string indent(region.declaration_column + kIndentWidth, ' ');
    std::string block;
    block += '\n';
    block += indent;
    block += "proves (";
    block += predicate_text(text, region.proposition, reformatted);
    block += ')';
    return block;
}

// Extends `span` to swallow all adjacent whitespace. The replacement block
// this produces always supplies its own leading '\n' per clause and (via
// `separator_after`) its own trailing separator, so any whitespace already
// around the region - on the same line or wrapped onto new ones - is
// redundant and must not survive alongside it.
source::ByteSpan widen_over_whitespace(std::string_view text, source::ByteSpan span) {
    std::size_t begin = span.offset;
    while (begin > 0 && is_horizontal_or_newline(text[begin - 1])) {
        --begin;
    }
    std::size_t end = span.end();
    while (end < text.size() && is_horizontal_or_newline(text[end])) {
        ++end;
    }
    return source::ByteSpan{begin, end - begin};
}

// What follows the last clause, canonically:
//   - a '{' (a definition's body) goes on its own line, back at the
//     declaration's own column, exactly as GRAMMAR.md's `verified`/`law`/
//     loop-header examples show the contract standing entirely apart from
//     the body it introduces;
//   - a ';' (a declaration with no body, e.g. `law ...;`) gets nothing -
//     clang-format never puts a space before a statement/declaration
//     terminator, and this layer does not fight that rule;
//   - anything else defensively gets a single space, though no clause kind
//     this engine emits is followed by anything else today.
std::string separator_after(std::string_view text, std::size_t offset, std::size_t declaration_column) {
    if (offset < text.size() && text[offset] == '{') {
        return "\n" + std::string(declaration_column, ' ');
    }
    if (offset < text.size() && text[offset] == ';') {
        return "";
    }
    return " ";
}

// `std::nullopt` when the region is already canonical: idempotency
// (formatting twice equals formatting once) depends on never emitting a
// same-text replacement, since a no-op edit would still show up as "this
// region needs re-checking" to a caller diffing edit counts, and a real
// editor would show a needless no-op change in its undo history.
std::optional<FormatEdit> make_clause_edit(std::string_view text, const ClauseRegion& region,
                                           const PredicateText& reformatted) {
    const source::ByteSpan widened = widen_over_whitespace(text, region.span);
    std::string block = canonical_clause_block(text, region.clauses, region.declaration_column, reformatted);
    block += separator_after(text, widened.end(), region.declaration_column);
    if (text.substr(widened.offset, widened.length) == block) {
        return std::nullopt;
    }
    return FormatEdit{widened, std::move(block)};
}

std::optional<FormatEdit> make_proves_edit(std::string_view text, const ProofRegion& region,
                                           const PredicateText& reformatted) {
    const source::ByteSpan widened = widen_over_whitespace(text, region.span);
    std::string block = canonical_proves_block(text, region, reformatted);
    block += separator_after(text, widened.end(), region.declaration_column);
    if (text.substr(widened.offset, widened.length) == block) {
        return std::nullopt;
    }
    return FormatEdit{widened, std::move(block)};
}

bool spans_overlap(source::ByteSpan lhs, source::ByteSpan rhs) {
    return lhs.offset < rhs.end() && rhs.offset < lhs.end();
}

// Proof-arm layout (GRAMMAR.md 5.6-5.8): `cases`/`decompose`/`induction`
// share one canonical arm block - `label(bindings) => { ... }`, one blank
// line between consecutive arms, nested arms indented one level deeper than
// their enclosing arm - collected the same way clause regions are: one
// region per statement with arms, a canonical replacement built from it, an
// edit only when that replacement differs from source.
struct ArmRegion {
    const frontend::ProofStatement* statement = nullptr;
    std::size_t declaration_column = 0; // the cases/decompose/induction keyword's own column
};

// Every OUTERMOST Cases/Decompose/Induction statement directly in
// `statements` (a proof body's own top-level statements, never an arm's -
// those are visited only through their enclosing statement's own edit).
// Nested Cases/Decompose/Induction (SPEC.md 2277: "Nested cases, decompose
// and induction are permitted") are deliberately NOT collected here as their
// own top-level region: `canonical_arm_block` already substitutes a nested
// statement's canonical block recursively while building its parent's
// replacement text, so a separate top-level edit for the same (now nested)
// byte span would overlap the parent's edit and violate the "edits are
// non-overlapping" contract every caller of this engine relies on.
void collect_arm_regions(const frontend::TokenStream& stream, const std::vector<frontend::ProofStatement>& statements,
                         std::vector<ArmRegion>& regions) {
    for (const frontend::ProofStatement& statement : statements) {
        if (statement.arms_span.length != 0) {
            regions.push_back(
                ArmRegion{&statement, layout_depth_at(stream, statement.arms_span.offset) * kIndentWidth});
        }
    }
}

// The canonical arm header: `label`, then `(binders)` with no space before
// '(' when there are any (`proof_arm_binders_have_no_space_before_binding_
// parenthesis`), never invented empty parentheses when there are none
// (`valueless_residual_arm_has_no_invented_binder_parentheses`). A template-
// indexed label (`alternative<0>`) is already part of `arm.label`'s own span
// (recognizer.cpp reads the '<...>' into the label itself), so it is used
// verbatim rather than reconstructed.
std::string canonical_arm_header(std::string_view text, const frontend::ProofArm& arm) {
    std::string header(text.substr(arm.label.offset, arm.label.length));
    if (!arm.binders.empty()) {
        header += '(';
        for (std::size_t i = 0; i < arm.binders.size(); ++i) {
            if (i != 0)
                header += ", ";
            header += arm.binders[i];
        }
        header += ')';
    }
    header += " => {";
    return header;
}

std::string canonical_arm_block(std::string_view text, const frontend::ProofStatement& statement,
                                std::size_t declaration_column);

// An arm body's statements, reindented one level past the arm header and
// with any nested Cases/Decompose/Induction statement replaced by its own
// canonical arm block. Primitive statements (`refl;`, `exact h;`, ...) and
// any comment between them are copied byte-for-byte from source, only their
// line's leading indentation changes (`proof_comments_are_preserved`,
// `primitive_proof_commands_remain_statements_not_call_syntax`): this layer
// lays out arms, it does not reflow proof-statement text clang-format never
// owned in the first place (C++L proof syntax, not C++).
std::string canonical_arm_body(std::string_view text, const frontend::ProofArm& arm, std::size_t body_column) {
    const std::string indent(body_column, ' ');
    std::string body;

    // Nested Cases/Decompose/Induction statements, in source order, so their
    // own already-canonical `arms_span` can be substituted into the copied
    // body text below instead of copied verbatim.
    std::vector<const frontend::ProofStatement*> nested;
    for (const frontend::ProofStatement& statement : arm.statements) {
        if (statement.arms_span.length != 0) {
            nested.push_back(&statement);
        }
    }

    // Reindents every non-blank line of a run of copied source to
    // `body_column`: the source's own leading whitespace on each line is
    // replaced, nothing else is reflowed. A blank line is dropped rather than
    // preserved, so reformatting an already-canonical body (which itself has
    // a structural blank line right after '{' and right before '}', from the
    // previous pass's own layout) stays idempotent instead of accumulating
    // one more blank line on every pass.
    //
    // This runs on the copied segments only, never on a substituted nested
    // block. `canonical_arm_block` returns that block already laid out, with
    // its arms indented relative to their own statement; flattening it to a
    // single column here would discard exactly that structure and collapse a
    // nested `cases` onto one level.
    const auto reindent = [&indent](std::string_view segment) {
        std::string out;
        std::size_t line_start = 0;
        while (line_start <= segment.size()) {
            std::size_t line_end = segment.find('\n', line_start);
            const bool last = line_end == std::string_view::npos;
            if (last)
                line_end = segment.size();
            const std::string_view line = segment.substr(line_start, line_end - line_start);
            const std::size_t content = line.find_first_not_of(" \t\r");
            if (content != std::string_view::npos) {
                out += indent;
                out += line.substr(content);
                out += '\n';
            }
            if (last)
                break;
            line_start = line_end + 1;
        }
        return out;
    };

    std::size_t cursor = arm.body_span.offset + 1;   // past the arm's own '{'
    const std::size_t end = arm.body_span.end() - 1; // before the arm's own '}'
    for (const frontend::ProofStatement* statement : nested) {
        // The nested statement's own `cases <subject> ` header is the tail of
        // the copied run, and `reindent` ends every line it emits with a
        // newline. The substituted block opens with its '{', which belongs on
        // that header's line, so the newline is removed before appending.
        std::string leading = reindent(text.substr(cursor, statement->arms_span.offset - cursor));
        while (!leading.empty() && is_horizontal_or_newline(leading.back())) {
            leading.pop_back();
        }
        body += leading;
        body += ' ';
        body += canonical_arm_block(text, *statement, body_column);
        body += '\n';
        cursor = statement->arms_span.end();
    }
    body += reindent(text.substr(cursor, end - cursor));

    while (!body.empty() && is_horizontal_or_newline(body.back())) {
        body.pop_back();
    }
    return body;
}

// The full `{ arm label(bindings) => { ... } ... }` block for one
// Cases/Decompose/Induction statement, arms separated by exactly one blank
// line, nested one level deeper than `declaration_column`.
std::string canonical_arm_block(std::string_view text, const frontend::ProofStatement& statement,
                                std::size_t declaration_column) {
    const std::string arm_indent(declaration_column + kIndentWidth, ' ');
    std::string block = "{";
    for (std::size_t i = 0; i < statement.arms.size(); ++i) {
        const frontend::ProofArm& arm = statement.arms[i];
        block += '\n';
        block += arm_indent;
        block += canonical_arm_header(text, arm);
        const std::string arm_body = canonical_arm_body(text, arm, declaration_column + 2 * kIndentWidth);
        if (!arm_body.empty()) {
            block += '\n';
            block += arm_body;
        }
        block += '\n';
        block += arm_indent;
        block += '}';
        if (i + 1 < statement.arms.size()) {
            block += "\n"; // one blank line between consecutive arms
        }
    }
    block += '\n';
    block += std::string(declaration_column, ' ');
    block += '}';
    return block;
}

std::optional<FormatEdit> make_arm_edit(std::string_view text, const ArmRegion& region) {
    std::string block = canonical_arm_block(text, *region.statement, region.declaration_column);

    // Only widened forward: the subject/keyword right before `arms_span`
    // ('cases f ', 'induction n ') is ordinary declarator-adjacent text this
    // layer does not own, unlike a clause's leading separator. Trailing
    // whitespace up to whatever follows IS this block's to own, the same way
    // `separator_after` owns a clause's trailing separator - most often
    // another '}' immediately closing the enclosing arm/proof body, which
    // needs its own line rather than sharing this block's closing line.
    source::ByteSpan widened = region.statement->arms_span;
    std::size_t end = widened.end();
    while (end < text.size() && is_horizontal_or_newline(text[end])) {
        ++end;
    }
    widened.length = end - widened.offset;
    if (end < text.size() && text[end] == '}') {
        block += '\n';
        block +=
            std::string(region.declaration_column >= kIndentWidth ? region.declaration_column - kIndentWidth : 0, ' ');
    } else if (end < text.size() && text[end] != ';') {
        block += ' ';
    }

    if (text.substr(widened.offset, widened.length) == block) {
        return std::nullopt;
    }
    return FormatEdit{widened, std::move(block)};
}

// Decodes the handful of XML entities clang-format's own writer emits
// (`clang::tooling::Replacements`' XML output escapes '&', '<', '>' as named
// entities and every other special byte, notably newlines, as a numeric
// character reference `&#N;`). This is not a general XML parser: it only
// has to round-trip exactly what that one writer produces.
std::string decode_xml_text(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out += text[i];
            continue;
        }
        const std::size_t semicolon = text.find(';', i);
        if (semicolon == std::string_view::npos) {
            out += text[i];
            continue;
        }
        const std::string_view entity = text.substr(i + 1, semicolon - i - 1);
        if (entity == "amp") {
            out += '&';
        } else if (entity == "lt") {
            out += '<';
        } else if (entity == "gt") {
            out += '>';
        } else if (entity == "quot") {
            out += '"';
        } else if (entity == "apos") {
            out += '\'';
        } else if (!entity.empty() && entity.front() == '#') {
            const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
            const std::string_view digits = entity.substr(hex ? 2 : 1);
            unsigned int code_point = 0;
            const auto parsed =
                std::from_chars(digits.data(), digits.data() + digits.size(), code_point, hex ? 16 : 10);
            if (parsed.ec == std::errc{} && code_point < 0x80) {
                out += static_cast<char>(code_point);
            }
        } else {
            out += text.substr(i, semicolon - i + 1); // unrecognized: keep verbatim
            i = semicolon;
            continue;
        }
        i = semicolon;
    }
    return out;
}

// Parses exactly the shape `clang-format --output-replacements-xml` writes:
// a flat sequence of `<replacement offset='N' length='N'>text</replacement>`
// elements. Offsets/lengths are always plain decimal attributes in that
// output, so this never needs general XML attribute parsing either.
std::vector<FormatEdit> parse_replacements_xml(std::string_view xml) {
    std::vector<FormatEdit> edits;
    std::size_t cursor = 0;
    while (true) {
        const std::size_t tag = xml.find("<replacement ", cursor);
        if (tag == std::string_view::npos) {
            break;
        }
        const std::size_t tag_close = xml.find('>', tag);
        if (tag_close == std::string_view::npos) {
            break;
        }
        const std::string_view attributes = xml.substr(tag, tag_close - tag);

        auto attribute_value = [&](std::string_view name) -> std::optional<std::size_t> {
            const std::string needle = std::string(name) + "='";
            const std::size_t start = attributes.find(needle);
            if (start == std::string_view::npos) {
                return std::nullopt;
            }
            const std::size_t value_start = start + needle.size();
            const std::size_t value_end = attributes.find('\'', value_start);
            if (value_end == std::string_view::npos) {
                return std::nullopt;
            }
            std::size_t value = 0;
            const auto parsed = std::from_chars(attributes.data() + value_start, attributes.data() + value_end, value);
            return parsed.ec == std::errc{} ? std::optional<std::size_t>(value) : std::nullopt;
        };

        const std::optional<std::size_t> offset = attribute_value("offset");
        const std::optional<std::size_t> length = attribute_value("length");
        const std::size_t end_tag = xml.find("</replacement>", tag_close);
        if (!offset.has_value() || !length.has_value() || end_tag == std::string_view::npos) {
            break;
        }
        const std::string_view raw_text = xml.substr(tag_close + 1, end_tag - tag_close - 1);
        edits.push_back(FormatEdit{source::ByteSpan{*offset, *length}, decode_xml_text(raw_text)});
        cursor = end_tag + std::string_view("</replacement>").size();
    }
    return edits;
}

// Runs clang-format over a scratch copy of `text`, restricted to `line_ranges`
// (each a 1-based inclusive [first, last] pair), and returns its reported
// replacements as `FormatEdit`s with ORIGINAL byte offsets. This is how
// ordinary-C++ regions are formatted: clang-format's own `-lines` scoping and
// `--output-replacements-xml` reporting, never a whole-file diff
// (AGENTS.md 14: delegate ordinary C++ to Clang-family tooling; never
// reimplement or approximate what it already does correctly).
//
// `excluded_spans` are the C++L clause spans (already widened over
// whitespace): clang-format is never asked to interpret their tokens.
//
// clang-format needs the WHOLE file, unrestricted by "-lines", to compute
// correct brace-depth/indentation for anything nested inside braces - verified
// directly: restricting "-lines" to only the ordinary-C++ gaps produced wrong
// indentation for a loop nested inside another loop, because clang-format
// never saw the outer loop's own brace open while indenting the inner one.
// But formatting the file totally unrestricted re-merges a relocated clause
// block back onto one line, since a bare `invariant (...)`/`expects (...)` call
// looks like ordinary joinable C++ to it (also verified directly). Blanking
// each excluded span - replacing its bytes with spaces, one line's worth of
// newlines preserved, exactly `frontend::project`'s own technique in
// `compiler/frontend/src/projection.cpp` for the same reason - resolves the
// tension: clang-format sees the true, complete brace structure (a blank
// clause span still occupies its lines, so nothing shifts), has no C++L
// tokens left to misjoin, and every replacement it reports is then scoped to
// `line_ranges` and re-checked against `excluded_spans` before being kept.
std::vector<FormatEdit> format_ordinary_cpp_lines(std::string_view text,
                                                  const std::vector<std::pair<std::size_t, std::size_t>>& line_ranges,
                                                  const std::vector<source::ByteSpan>& excluded_spans,
                                                  const std::string& clang_format, const std::string& style_config,
                                                  const std::string& stem, std::vector<diagnostics::Diagnostic>& out) {
    if (line_ranges.empty()) {
        return {};
    }

    std::string masked(text);
    for (source::ByteSpan span : excluded_spans) {
        for (std::size_t offset = span.offset; offset < span.end() && offset < masked.size(); ++offset) {
            if (masked[offset] != '\n') {
                masked[offset] = ' ';
            }
        }
    }

    const driver::ScratchDirectory scratch;
    if (scratch.path().empty()) {
        report(out, diagnostics::Severity::Error, diagnostics::Category::Internal,
               "could not create an isolated formatting directory");
        return {};
    }

    const std::filesystem::path source_path = scratch.path() / (stem.empty() ? "buffer.cpp" : stem);
    if (!driver::write_scratch_file(source_path, masked)) {
        report(out, diagnostics::Severity::Error, diagnostics::Category::Internal,
               "could not write a scratch copy of '" + stem + "'");
        return {};
    }

    const std::string style = style_config.empty() ? "-style=LLVM" : "-style=file:" + style_config;
    const std::string tool = clang_format.empty() ? std::string{CPPL_DEFAULT_CLANG_FORMAT} : clang_format;

    const std::vector<std::string> arguments{style, "--output-replacements-xml", source_path.string()};

    const driver::ProcessResult result =
        driver::run_capturing_stdout(tool, arguments, scratch.path() / "replacements.xml");
    if (!result.started) {
        report(out, diagnostics::Severity::Error, diagnostics::Category::Internal,
               "could not run '" + tool + "': " + result.error);
        return {};
    }
    if (result.exit_code != 0) {
        report(out, diagnostics::Severity::Error, diagnostics::Category::Internal,
               "'" + tool + "' failed (exit code " + std::to_string(result.exit_code) + ")");
        return {};
    }

    const std::optional<std::string> xml = read_file(scratch.path() / "replacements.xml");
    if (!xml.has_value()) {
        report(out, diagnostics::Severity::Error, diagnostics::Category::Internal,
               "could not read clang-format's replacements for '" + stem + "'");
        return {};
    }

    // Keep only replacements inside a requested line range and outside every
    // excluded (blanked) span - the latter should already be empty since
    // clang-format has no tokens left there to reformat, but a boundary edit
    // (e.g. the whitespace run right after a blanked span) is checked by
    // exact byte overlap all the same, never by which line it touches.
    std::vector<FormatEdit> edits = parse_replacements_xml(*xml);
    std::erase_if(edits, [&](const FormatEdit& edit) {
        const bool excluded =
            std::ranges::any_of(excluded_spans, [&](source::ByteSpan span) { return spans_overlap(edit.span, span); });
        const bool in_range = std::ranges::any_of(line_ranges, [&](const auto& range) {
            const std::size_t first_line = line_number(text, edit.span.offset);
            const std::size_t last_line =
                line_number(text, edit.span.length == 0 ? edit.span.offset : edit.span.end() - 1);
            return first_line >= range.first && last_line <= range.second;
        });
        return excluded || !in_range;
    });
    return edits;
}

// Runs clang-format once over the UNMASKED original document, restricted via
// repeated `--offset`/`--length` pairs to exactly `spans` (clause/proof/
// refinement predicate byte ranges), and returns its replacements as
// `FormatEdit`s in ORIGINAL byte offsets, sorted by offset.
//
// Predicates are reformatted this way, rather than by hand-rolling operator
// spacing, per AGENTS.md 14: Clang is the authority on C++ expression syntax.
// clang-format's `-offset`/`-length` restriction (unlike `-lines`) still sees
// the true, complete, UNMASKED document while only being permitted to touch
// bytes inside the given ranges - verified directly: it reformats `x>=0`
// inside `expects(x>=0)` to `x >= 0` and reports no replacement whose span
// reaches outside the predicate, so the surrounding clause syntax (which this
// layer owns, not clang-format) is never at risk of being rejoined or
// reinterpreted by this call.
std::vector<FormatEdit> format_expression_spans(std::string_view text, const std::vector<source::ByteSpan>& spans,
                                                const std::string& clang_format, const std::string& style_config,
                                                const std::string& stem, std::vector<diagnostics::Diagnostic>& out) {
    if (spans.empty()) {
        return {};
    }

    const driver::ScratchDirectory scratch;
    if (scratch.path().empty()) {
        report(out, diagnostics::Severity::Error, diagnostics::Category::Internal,
               "could not create an isolated formatting directory");
        return {};
    }

    const std::filesystem::path source_path = scratch.path() / (stem.empty() ? "buffer.cpp" : stem);
    if (!driver::write_scratch_file(source_path, text)) {
        report(out, diagnostics::Severity::Error, diagnostics::Category::Internal,
               "could not write a scratch copy of '" + stem + "'");
        return {};
    }

    const std::string style = style_config.empty() ? "-style=LLVM" : "-style=file:" + style_config;
    const std::string tool = clang_format.empty() ? std::string{CPPL_DEFAULT_CLANG_FORMAT} : clang_format;

    std::vector<std::string> arguments{style, "--output-replacements-xml"};
    for (source::ByteSpan span : spans) {
        arguments.push_back("--offset=" + std::to_string(span.offset));
        arguments.push_back("--length=" + std::to_string(span.length));
    }
    arguments.push_back(source_path.string());

    const driver::ProcessResult result =
        driver::run_capturing_stdout(tool, arguments, scratch.path() / "expr-replacements.xml");
    if (!result.started) {
        report(out, diagnostics::Severity::Error, diagnostics::Category::Internal,
               "could not run '" + tool + "': " + result.error);
        return {};
    }
    if (result.exit_code != 0) {
        report(out, diagnostics::Severity::Error, diagnostics::Category::Internal,
               "'" + tool + "' failed (exit code " + std::to_string(result.exit_code) + ")");
        return {};
    }

    const std::optional<std::string> xml = read_file(scratch.path() / "expr-replacements.xml");
    if (!xml.has_value()) {
        report(out, diagnostics::Severity::Error, diagnostics::Category::Internal,
               "could not read clang-format's replacements for '" + stem + "'");
        return {};
    }

    std::vector<FormatEdit> edits = parse_replacements_xml(*xml);
    std::ranges::sort(edits,
                      [](const FormatEdit& lhs, const FormatEdit& rhs) { return lhs.span.offset < rhs.span.offset; });
    return edits;
}

// Applies `edits` (already restricted to fall inside `span`, as
// `format_expression_spans` guarantees) to the ORIGINAL text of `span`,
// returning the reformatted predicate text. `edits` must be sorted and
// non-overlapping, which `format_expression_spans`'s own clang-format output
// already is.
std::string apply_expression_edits(std::string_view text, source::ByteSpan span, const std::vector<FormatEdit>& edits) {
    std::string result;
    std::size_t cursor = span.offset;
    for (const FormatEdit& edit : edits) {
        if (edit.span.offset < span.offset || edit.span.end() > span.end()) {
            continue; // defensive: never let an out-of-range edit corrupt this predicate
        }
        result.append(text.substr(cursor, edit.span.offset - cursor));
        result += edit.replacement;
        cursor = edit.span.end();
    }
    result.append(text.substr(cursor, span.end() - cursor));
    return result;
}

} // namespace

namespace {

// One pass: relocate every C++L clause overlapping `ranges` and hand every
// other touched line to clang-format. A single pass is not always already at
// its fixed point (see `format_ranges` below), so this is internal.
FormatResult format_ranges_once(const FormatRequest& request, const std::vector<source::ByteSpan>& ranges) {
    FormatResult result;

    const frontend::TokenStream stream =
        frontend::lex(request.text, request.virtual_path.empty() ? "buffer.cpp" : request.virtual_path);
    diagnostics::Engine engine;
    const frontend::Syntax syntax = frontend::recognize(stream, engine, frontend::RecognitionMode::Edit);

    const std::vector<ClauseRegion> all_regions = collect_regions(stream, syntax);
    const std::vector<ProofRegion> all_proof_regions = collect_proof_regions(stream, syntax);
    std::vector<ArmRegion> all_arm_regions;
    for (const frontend::ProofDeclaration& proof : syntax.proofs) {
        collect_arm_regions(stream, proof.statements, all_arm_regions);
    }

    const bool whole_document = ranges.empty();
    // A zero-length range is a cursor position, not an empty interval: it
    // selects whatever region it sits inside, including right at that
    // region's own first byte (`spans_overlap`'s half-open `<`/`<` test
    // never holds for two spans of total length zero at the same offset, so
    // it is checked here as inclusive point-containment instead - the same
    // "smallest safe unit containing a position" contract `format_on_type`
    // already documents).
    auto overlaps_request = [&](source::ByteSpan span) {
        if (whole_document) {
            return true;
        }
        return std::ranges::any_of(ranges, [&](source::ByteSpan range) {
            if (range.length == 0) {
                return range.offset >= span.offset && range.offset <= span.end();
            }
            return spans_overlap(range, span);
        });
    };

    const std::filesystem::path virtual_path(request.virtual_path);
    std::string stem = virtual_path.filename().string();
    if (stem.empty()) {
        stem = "buffer.cpp";
    }
    const std::string style_config =
        request.style_config.empty() ? std::string{CPPL_REPO_CLANG_FORMAT_CONFIG} : request.style_config;

    // Every predicate this pass may relocate, reformatted through Clang in one
    // batched call (AGENTS.md 14: never reimplement C++ expression spacing).
    // Collected up front so `canonical_clause_block`/`canonical_proves_block`/
    // the `where` replacement below can all read back already-canonical
    // predicate text instead of copying source bytes verbatim.
    // A predicate spelling C++L-only syntax is withheld from the batch: see
    // `has_cppl_only_syntax`. `predicate_text` then falls back to its source
    // bytes, which is the canonical form for those constructs.
    std::vector<source::ByteSpan> predicate_spans;
    predicate_spans.reserve(syntax.refinement_types.size());
    const auto collect = [&](source::ByteSpan span) {
        if (!has_cppl_only_syntax(request.text.substr(span.offset, span.length))) {
            predicate_spans.push_back(span);
        }
    };
    for (const auto& refinement : syntax.refinement_types) {
        collect(refinement.predicate);
    }
    for (const ClauseRegion& region : all_regions) {
        for (const frontend::Clause* clause : region.clauses) {
            collect(clause->expression);
        }
    }
    for (const ProofRegion& region : all_proof_regions) {
        collect(region.proposition);
    }

    PredicateText reformatted_predicates;
    if (!predicate_spans.empty()) {
        std::vector<diagnostics::Diagnostic> predicate_diagnostics;
        const std::vector<FormatEdit> predicate_edits = format_expression_spans(
            request.text, predicate_spans, request.clang_format, style_config, stem, predicate_diagnostics);
        result.diagnostics.insert(result.diagnostics.end(), predicate_diagnostics.begin(), predicate_diagnostics.end());
        for (source::ByteSpan span : predicate_spans) {
            std::vector<FormatEdit> within;
            std::ranges::copy_if(predicate_edits, std::back_inserter(within),
                                 [&](const FormatEdit& edit) { return spans_overlap(edit.span, span); });
            reformatted_predicates.emplace(span.offset, apply_expression_edits(request.text, span, within));
        }
    }

    std::vector<FormatEdit> edits;
    std::vector<source::ByteSpan> cppl_spans; // excluded from the ordinary-C++ line ranges below

    // `where` is part of the refinement declaration, not a continuation
    // clause. Mask its predicate from clang-format just as other clauses are
    // masked; C++ function-call spacing must not change specification spacing.
    for (const auto& refinement : syntax.refinement_types) {
        const auto where = std::ranges::find_if(stream.tokens(), [&](const auto& token) {
            return token.is_identifier("where") && token.span.offset >= refinement.base.end() &&
                   token.span.end() < refinement.predicate.offset;
        });
        if (where == stream.tokens().end())
            continue;
        const source::ByteSpan span{where->span.offset, refinement.predicate.end() + 1 - where->span.offset};
        cppl_spans.push_back(span);
        const std::string replacement =
            "where (" + std::string(predicate_text(request.text, refinement.predicate, reformatted_predicates)) + ")";
        if (overlaps_request(span) && stream.spelling(span) != replacement)
            edits.push_back({span, replacement});
    }

    for (const ClauseRegion& region : all_regions) {
        if (const std::optional<source::ByteSpan> header_break =
                template_header_break(request.text, region.declaration_offset);
            header_break.has_value()) {
            cppl_spans.push_back(*header_break);
            if (overlaps_request(*header_break) &&
                request.text.substr(header_break->offset, header_break->length) != "\n")
                edits.push_back({*header_break, "\n"});
        }
        if (!overlaps_request(region.span)) {
            continue;
        }
        if (std::optional<FormatEdit> edit = make_clause_edit(request.text, region, reformatted_predicates);
            edit.has_value()) {
            edits.push_back(std::move(*edit));
        }
        cppl_spans.push_back(widen_over_whitespace(request.text, region.span));
    }
    for (const ProofRegion& region : all_proof_regions) {
        if (!overlaps_request(region.span)) {
            continue;
        }
        if (std::optional<FormatEdit> edit = make_proves_edit(request.text, region, reformatted_predicates);
            edit.has_value()) {
            edits.push_back(std::move(*edit));
        }
        cppl_spans.push_back(widen_over_whitespace(request.text, region.span));
    }
    for (const ArmRegion& region : all_arm_regions) {
        if (!overlaps_request(region.statement->arms_span)) {
            continue;
        }
        if (std::optional<FormatEdit> edit = make_arm_edit(request.text, region); edit.has_value()) {
            edits.push_back(std::move(*edit));
        }
        // `arms_span` opens at the '{', leaving the `cases <subject>` header
        // and the newline before it visible to clang-format, which reads the
        // header as a statement it may join to the enclosing body's own '{'
        // ("{cases s {"). The mask therefore starts at the keyword's line, so
        // the whole C++L statement is withheld, as the clause path above does.
        source::ByteSpan arms = region.statement->arms_span;
        const std::size_t line_start = request.text.rfind('\n', arms.offset);
        const std::size_t keyword = line_start == std::string::npos ? 0 : line_start;
        cppl_spans.push_back(widen_over_whitespace(request.text, {keyword, arms.end() - keyword}));
    }

    // The line ranges to ask clang-format to look at: the requested range(s),
    // or the whole document. This deliberately does NOT carve out the lines a
    // C++L clause above already claimed: clang-format is allowed to have an
    // opinion about a boundary a clause touches (e.g. the space between a
    // proof's closing ')' and its body's '{' on the same physical line,
    // 'proves (p) { refl; }' - verified directly, clang-format wants to expand
    // that inline body under this repo's own AllowShortBlocksOnASingleLine:
    // Never, regardless of the clause sharing its line). Excluding the whole
    // line here would silently hide that legitimate ordinary-C++ edit; the
    // exact byte-overlap filter in `format_ordinary_cpp_lines` is what
    // actually protects clause text, at byte granularity instead of line
    // granularity, so this can safely stay permissive.
    std::vector<std::pair<std::size_t, std::size_t>> line_ranges;
    const std::size_t total_lines = line_number(request.text, request.text.size());
    if (whole_document) {
        line_ranges.emplace_back(1, total_lines);
    } else {
        for (source::ByteSpan range : ranges) {
            const std::size_t first = line_number(request.text, range.offset);
            const std::size_t last =
                line_number(request.text, range.end() == range.offset ? range.offset : range.end() - 1);
            line_ranges.emplace_back(first, last);
        }
    }

    std::vector<FormatEdit> ordinary_edits = format_ordinary_cpp_lines(
        request.text, line_ranges, cppl_spans, request.clang_format, style_config, stem, result.diagnostics);
    edits.insert(edits.end(), std::make_move_iterator(ordinary_edits.begin()),
                 std::make_move_iterator(ordinary_edits.end()));

    std::ranges::sort(edits,
                      [](const FormatEdit& lhs, const FormatEdit& rhs) { return lhs.span.offset < rhs.span.offset; });

    result.ok = std::ranges::none_of(
        result.diagnostics, [](const auto& diagnostic) { return diagnostic.severity == diagnostics::Severity::Error; });
    result.edits = std::move(edits);
    return result;
}

} // namespace

FormatResult format_ranges(const FormatRequest& request, const std::vector<source::ByteSpan>& ranges) {
    return format_ranges_once(request, ranges);
}

FormatResult format_document(const FormatRequest& request) {
    return format_ranges(request, {});
}

// Whether `trigger_character` is one this engine treats as "a syntactic unit
// was just completed," the only case worth reformatting a whole enclosing
// C++L clause for. An ordinary letter/digit typed mid-token (an editor may
// call `format_on_type` on every keystroke) is not such a signal: relocating
// the entire clause on every letter would repeatedly rewrite text around the
// cursor the developer has not finished typing, contradicting "never
// reformats more than what was just typed" for a unit far larger than one
// keystroke. This mirrors real LSP clients, which likewise register only a
// curated set of trigger characters for on-type formatting, never arbitrary
// identifier characters.
bool completes_a_syntactic_unit(const std::string& trigger_character) {
    return trigger_character == ")" || trigger_character == ";" || trigger_character == "}" ||
           trigger_character == "\n" || trigger_character == "\r";
}

FormatResult format_on_type(const FormatRequest& request, std::size_t position, const std::string& trigger_character) {
    const frontend::TokenStream stream =
        frontend::lex(request.text, request.virtual_path.empty() ? "buffer.cpp" : request.virtual_path);
    diagnostics::Engine engine;
    const frontend::Syntax syntax = frontend::recognize(stream, engine, frontend::RecognitionMode::Edit);

    const bool structural_trigger = completes_a_syntactic_unit(trigger_character);
    for (const ClauseRegion& region : collect_regions(stream, syntax)) {
        if (position >= region.span.offset && position <= region.span.end()) {
            if (!structural_trigger) {
                // A non-structural keystroke inside a clause is not a signal
                // to relocate it, and the enclosing-line fallback below would
                // still reach the clause on this line regardless of how
                // narrowly it is scoped (the two share a line), so this
                // engine makes no edit rather than reaching outside the
                // clause it has no reason to touch yet.
                FormatResult result;
                result.ok = true;
                return result;
            }
            return format_ranges(request, {region.span});
        }
    }
    for (const ProofRegion& region : collect_proof_regions(stream, syntax)) {
        if (position >= region.span.offset && position <= region.span.end()) {
            if (!structural_trigger) {
                FormatResult result;
                result.ok = true;
                return result;
            }
            return format_ranges(request, {region.span});
        }
    }

    // Not inside a recognized C++L clause: format only the enclosing line,
    // conservatively, rather than guessing at a larger ordinary-C++ unit.
    const std::size_t line_start = request.text.rfind('\n', position == 0 ? 0 : position - 1);
    const std::size_t start = (line_start == std::string_view::npos) ? 0 : line_start + 1;
    std::size_t end = request.text.find('\n', position);
    end = (end == std::string_view::npos) ? request.text.size() : end;
    if (start >= end) {
        FormatResult result;
        result.ok = true;
        return result; // an empty line: nothing to format
    }
    return format_ranges(request, {source::ByteSpan{start, end - start}});
}

namespace {

// True when `keyword_offset` is preceded on its line only by exactly
// `expected_column` spaces (the canonical indentation: one level past the
// declaration, and nothing else sharing the line).
bool at_canonical_column(std::string_view text, std::size_t keyword_offset, std::size_t expected_column) {
    if (line_start_column(text, keyword_offset) != expected_column) {
        return false;
    }
    const std::size_t line_start = keyword_offset - expected_column;
    return text.substr(line_start, expected_column).find_first_not_of(' ') == std::string_view::npos;
}

// Exactly one space between a C++L clause keyword and its '(': `expects` and
// friends are not C++ control statements, so this repo's own
// SpaceBeforeParens: ControlStatements rule was never meant to apply to them.
bool has_canonical_spacing(std::string_view text, source::ByteSpan keyword) {
    return keyword.end() < text.size() && text.substr(keyword.end(), 2) == " (";
}

} // namespace

// Reuses exactly the region detection the formatting engine uses: a style
// violation is nothing more than "this clause's actual column differs from
// the column the engine would place it at" (ARCHITECTURE.md: one engine, not
// a second, drifting notion of what canonical means).
std::vector<diagnostics::Diagnostic> check_style(const frontend::TokenStream& stream, const frontend::Syntax& syntax) {
    std::vector<diagnostics::Diagnostic> out;
    const std::string_view text = stream.text();

    for (const ClauseRegion& region : collect_regions(stream, syntax)) {
        const std::size_t expected = region.declaration_column + kIndentWidth;
        for (const frontend::Clause* clause : region.clauses) {
            if (!at_canonical_column(text, clause->keyword.offset, expected)) {
                diagnostics::Diagnostic diagnostic;
                diagnostic.severity = diagnostics::Severity::Warning;
                diagnostic.category = diagnostics::Category::Style;
                diagnostic.message =
                    "'" + std::string(keyword_spelling(clause->kind)) +
                    "' should begin its own continuation line, indented one level from the declaration";
                diagnostic.location = clause->location;
                out.push_back(std::move(diagnostic));
            }
            if (!has_canonical_spacing(text, clause->keyword)) {
                diagnostics::Diagnostic diagnostic;
                diagnostic.severity = diagnostics::Severity::Warning;
                diagnostic.category = diagnostics::Category::Style;
                diagnostic.message =
                    "'" + std::string(keyword_spelling(clause->kind)) + "' requires one space before '('";
                diagnostic.location = clause->location;
                out.push_back(std::move(diagnostic));
            }
        }
    }

    for (const ProofRegion& region : collect_proof_regions(stream, syntax)) {
        const std::size_t expected = region.declaration_column + kIndentWidth;
        // The proves keyword's own byte offset is the region's span start.
        if (!at_canonical_column(text, region.span.offset, expected)) {
            diagnostics::Diagnostic diagnostic;
            diagnostic.severity = diagnostics::Severity::Warning;
            diagnostic.category = diagnostics::Category::Style;
            diagnostic.message =
                "'proves' should begin its own continuation line, indented one level from the declaration";
            diagnostic.location = region.location;
            out.push_back(std::move(diagnostic));
        }
        if (!has_canonical_spacing(text, source::ByteSpan{region.span.offset, std::string_view("proves").size()})) {
            diagnostics::Diagnostic diagnostic;
            diagnostic.severity = diagnostics::Severity::Warning;
            diagnostic.category = diagnostics::Category::Style;
            diagnostic.message = "'proves' requires one space before '('";
            diagnostic.location = region.location;
            out.push_back(std::move(diagnostic));
        }
    }

    return out;
}

std::vector<SyntaxFix> syntax_fixes(const FormatRequest& request) {
    const auto stream = frontend::lex(request.text, request.virtual_path);
    diagnostics::Engine engine;
    const auto syntax = frontend::recognize(stream, engine, frontend::RecognitionMode::Edit);
    std::vector<SyntaxFix> fixes;
    for (const auto& law : syntax.laws) {
        const bool has_result = std::ranges::any_of(stream.tokens(), [&](const auto& token) {
            return token.span.offset >= law.range.span.offset && token.span.end() <= law.range.span.end() &&
                   token.is_identifier("result");
        });
        if (has_result)
            continue;
        for (const auto& clause : law.clauses) {
            if (clause.kind == frontend::ClauseKind::Ensures)
                fixes.push_back({"Replace Law 'ensures' with 'proves'", {{clause.keyword, "proves"}}});
        }
    }
    for (const auto& proof : syntax.proofs) {
        for (const auto& token : stream.tokens()) {
            if (token.span.offset >= proof.range.span.offset && token.span.end() <= proof.range.span.end() &&
                token.is_identifier("case"))
                fixes.push_back({"Use proof-only 'cases'", {{token.span, "cases"}}});
        }
    }
    return fixes;
}

} // namespace cppl::formatter
