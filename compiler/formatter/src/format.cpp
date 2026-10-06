#include "cppl/formatter/format.hpp"

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "format_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::formatter {

using detail::apply_expression_edits;
using detail::ArmRegion;
using detail::ClauseRegion;
using detail::collect_arm_regions;
using detail::collect_proof_regions;
using detail::collect_regions;
using detail::format_expression_spans;
using detail::format_ordinary_cpp_lines;
using detail::has_cppl_only_syntax;
using detail::keyword_spelling;
using detail::kIndentWidth;
using detail::layout_depth_at;
using detail::line_number;
using detail::line_start_column;
using detail::make_arm_edit;
using detail::make_clause_edit;
using detail::make_proves_edit;
using detail::predicate_text;
using detail::PredicateText;
using detail::ProofRegion;
using detail::spans_overlap;
using detail::split_column;
using detail::template_header_break;
using detail::widen_over_whitespace;

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
    // A case split on a runtime path lays its arms out as one in a proof body
    // does (GRAMMAR.md 49).
    for (const frontend::PathCaseSplit& split : syntax.path_splits) {
        if (split.statement.arms_span.length != 0) {
            all_arm_regions.push_back(
                ArmRegion{&split.statement, split_column(stream, split.statement),
                          layout_depth_at(stream, split.statement.keyword.offset) * kIndentWidth});
        }
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
    const std::string& style_config = request.style_config;

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
    // A split on a runtime path is a statement of the body, withheld from
    // clang-format with its keyword's line above, so its keyword is indented
    // here to the body's own depth when it begins its line.
    for (const frontend::PathCaseSplit& split : syntax.path_splits) {
        const source::ByteSpan& keyword = split.statement.keyword;
        if (!overlaps_request(split.statement.arms_span)) {
            continue;
        }
        const std::size_t newline = request.text.rfind('\n', keyword.offset);
        const std::size_t start = newline == std::string::npos ? 0 : newline + 1;
        const std::string_view leading = std::string_view(request.text).substr(start, keyword.offset - start);
        if (leading.find_first_not_of(" \t") != std::string_view::npos) {
            continue;
        }
        const std::string indentation(split_column(stream, split.statement), ' ');
        if (leading != indentation) {
            edits.push_back({source::ByteSpan{start, leading.size()}, indentation});
        }
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

    // A C++L construct laid out anew would drop or move a directive written
    // inside it, or join it to another line, and so change the program; such
    // a construct is left exactly as written. A directive owns the line breaks
    // around it as well as its own text.
    std::erase_if(edits, [&](const FormatEdit& edit) {
        return std::ranges::any_of(stream.directives(), [&](const frontend::Directive& directive) {
            const std::size_t begin = directive.span.offset > 0 ? directive.span.offset - 1 : 0;
            const std::size_t end = std::min(directive.span.end() + 1, request.text.size());
            return spans_overlap(edit.span, source::ByteSpan{begin, end - begin});
        });
    });

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
