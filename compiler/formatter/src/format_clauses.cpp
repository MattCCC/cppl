// Laying out the clause blocks of declarations and loops, and proof blocks.

#include "cppl/formatter/format.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "format_detail.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cppl::formatter {

using detail::keyword_spelling;
using detail::kIndentWidth;
using detail::predicate_text;
using detail::PredicateText;
using detail::ProofRegion;

namespace detail {

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

} // namespace detail

namespace {

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

} // namespace

namespace detail {

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

} // namespace detail

namespace {

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

} // namespace

namespace detail {

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

// The column a case split on a runtime path starts at: the body's own depth,
// and one level more where the split is the unbraced body of `if`, `else`,
// `while`, `for` or `do`, as clang-format indents any other statement there.
std::size_t split_column(const frontend::TokenStream& stream, const frontend::ProofStatement& statement) {
    const std::size_t column = layout_depth_at(stream, statement.keyword.offset) * kIndentWidth;
    const std::vector<frontend::Token>& tokens = stream.tokens();
    const auto keyword = std::ranges::lower_bound(tokens, statement.keyword.offset, {},
                                                  [](const frontend::Token& token) { return token.span.offset; });
    if (keyword == tokens.begin()) {
        return column;
    }
    const auto before = static_cast<std::size_t>(keyword - tokens.begin()) - 1;
    if (tokens[before].is_identifier("else") || tokens[before].is_identifier("do")) {
        return column + kIndentWidth;
    }
    if (!tokens[before].is_punctuator(")")) {
        return column;
    }
    std::size_t depth = 0;
    for (std::size_t index = before + 1; index > 0; --index) {
        const frontend::Token& token = tokens[index - 1];
        if (token.is_punctuator(")")) {
            ++depth;
        } else if (token.is_punctuator("(") && --depth == 0) {
            const bool control =
                index >= 2 && (tokens[index - 2].is_identifier("if") || tokens[index - 2].is_identifier("while") ||
                               tokens[index - 2].is_identifier("for"));
            return control ? column + kIndentWidth : column;
        }
    }
    return column;
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
        if (const std::optional<frontend::Clause>& decreases = loop.decreases; decreases.has_value()) {
            region.clauses.push_back(&*decreases);
        }
        regions.push_back(std::move(region));
    }

    return regions;
}

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

} // namespace detail

namespace {

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

} // namespace

namespace detail {

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

} // namespace detail

namespace {

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

} // namespace

namespace detail {

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

} // namespace detail

} // namespace cppl::formatter
