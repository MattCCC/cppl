// Laying out the arms of a proof's case analysis.

#include "cppl/formatter/format.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "format_detail.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::formatter {

using detail::is_horizontal_or_newline;
using detail::kIndentWidth;

namespace detail {

// Every OUTERMOST Cases/Decompose/Induction statement directly in
// `statements` (a proof body's own top-level statements, never an arm's -
// those are visited only through their enclosing statement's own edit).
// Nested Cases/Decompose/Induction (SPEC.md 20.3: "Nested cases, decompose
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

} // namespace detail

namespace {

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

// The comments written in the source between two positions of an arm block,
// laid out for the canonical block. A well-formed block holds nothing between
// its arms but whitespace and comments, and a comment is never dropped.
//
// `same_line` is what shares the line the gap starts on - a comment trailing
// the '{', '}' or ';' before it - and stays on that line. `lines` is every
// comment line after it, each on a line of its own at `indent`.
struct GapComments {
    std::string same_line;
    std::string lines;
};

GapComments comments_between(std::string_view text, std::size_t from, std::size_t to, const std::string& indent) {
    GapComments comments;
    if (from >= to) {
        return comments;
    }
    const std::string_view gap = text.substr(from, to - from);
    const auto trimmed = [](std::string_view line) {
        const std::size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string_view::npos) {
            return std::string_view{};
        }
        const std::size_t last = line.find_last_not_of(" \t\r");
        return line.substr(first, last - first + 1);
    };
    std::size_t line_start = 0;
    bool first_line = true;
    while (line_start <= gap.size()) {
        std::size_t line_end = gap.find('\n', line_start);
        const bool last = line_end == std::string_view::npos;
        if (last) {
            line_end = gap.size();
        }
        const std::string_view content = trimmed(gap.substr(line_start, line_end - line_start));
        if (!content.empty()) {
            if (first_line) {
                comments.same_line += ' ';
                comments.same_line += content;
            } else {
                comments.lines += '\n';
                comments.lines += indent;
                comments.lines += content;
            }
        }
        if (last) {
            break;
        }
        first_line = false;
        line_start = line_end + 1;
    }
    return comments;
}

// The full `{ arm label(bindings) => { ... } ... }` block for one
// Cases/Decompose/Induction statement, arms separated by exactly one blank
// line, nested one level deeper than `declaration_column`. Comments between
// the arms keep their place relative to them (`comments_between`).
std::string canonical_arm_block(std::string_view text, const frontend::ProofStatement& statement,
                                std::size_t declaration_column) {
    const std::string arm_indent(declaration_column + kIndentWidth, ' ');
    std::string block = "{";
    std::size_t previous = statement.arms_span.offset + 1; // just past the block's own '{'
    for (std::size_t i = 0; i < statement.arms.size(); ++i) {
        const frontend::ProofArm& arm = statement.arms[i];
        const GapComments gap = comments_between(text, previous, arm.span.offset, arm_indent);
        block += gap.same_line;
        if (i > 0) {
            block += '\n'; // one blank line between consecutive arms
        }
        block += gap.lines;
        previous = arm.span.end();
        block += '\n';
        block += arm_indent;
        // An omitted case has no body and no braces, so it is one line
        // (GRAMMAR.md 5.7) rather than a header/body/close. The statement after
        // `by` is copied as written, like every primitive proof statement, so
        // its evidence reference and arguments survive unchanged.
        if (arm.omitted) {
            block += "omit ";
            block += text.substr(arm.label.offset, arm.label.length);
            block += " by ";
            block += text.substr(arm.discharge_span.offset, arm.discharge_span.length);
            continue;
        }
        block += canonical_arm_header(text, arm);
        const std::string arm_body = canonical_arm_body(text, arm, declaration_column + 2 * kIndentWidth);
        if (!arm_body.empty()) {
            block += '\n';
            block += arm_body;
        }
        block += '\n';
        block += arm_indent;
        block += '}';
    }
    // Comments after the last arm stand apart from it, like one more arm would.
    const GapComments trailing = comments_between(text, previous, statement.arms_span.end() - 1, arm_indent);
    block += trailing.same_line;
    if (!trailing.lines.empty()) {
        block += '\n';
        block += trailing.lines;
    }
    block += '\n';
    block += std::string(declaration_column, ' ');
    block += '}';
    return block;
}

} // namespace

namespace detail {

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
        // Another statement of the same block, as follows a split on a runtime
        // path: it starts its own line.
        block += '\n';
        block += std::string(region.following_column.value_or(region.declaration_column), ' ');
    }

    if (text.substr(widened.offset, widened.length) == block) {
        return std::nullopt;
    }
    return FormatEdit{widened, std::move(block)};
}

} // namespace detail

} // namespace cppl::formatter
