#include "cppl/formatter/format.hpp"

#include "cppl/driver/process.hpp"
#include "cppl/driver/scratch.hpp"
#include "cppl/frontend/token.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <optional>
#include <sstream>

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
        case frontend::ClauseKind::Ensures:
            return "ensures";
        case frontend::ClauseKind::Expects:
            return "expects";
        case frontend::ClauseKind::Invariant:
            return "invariant";
    }
    return "";
}

// Every clause on its own line, indented one level past the declaration,
// exactly as GRAMMAR.md's `verified`/`law`/loop-header examples show. The
// predicate text itself is copied byte-for-byte from the input: this layer
// relocates clauses, it never reflows the expressions clang-format already
// owns.
//
// No whitespace stands between the clause keyword and its '(': `expects` is
// not a C++ control statement (`if`/`while`), it is C++L's own contract-clause
// syntax, and this repo's `.clang-format` SpaceBeforeParens: ControlStatements
// rule was never meant to reach it.
std::string canonical_clause_block(std::string_view text, const std::vector<const frontend::Clause*>& clauses,
                                   std::size_t declaration_column) {
    const std::string indent(declaration_column + kIndentWidth, ' ');
    std::string block;
    for (const frontend::Clause* clause : clauses) {
        block += '\n';
        block += indent;
        block += keyword_spelling(clause->kind);
        block += '(';
        block += text.substr(clause->expression.offset, clause->expression.length);
        block += ')';
    }
    return block;
}

// One region per law/verified-function clause block and loop invariant
// block. `where` on a refinement type is deliberately never visited: it
// stays inline (the request is explicit about this).
std::vector<ClauseRegion> collect_regions(std::string_view text, const frontend::Syntax& syntax) {
    std::vector<ClauseRegion> regions;

    for (const frontend::LawDeclaration& law : syntax.laws) {
        if (law.clauses.empty()) {
            continue;
        }
        ClauseRegion region;
        region.declaration_column = line_start_column(text, law.range.span.offset);
        const source::ByteSpan first = law.clauses.front().keyword;
        const source::ByteSpan last = law.clauses.back().expression;
        region.span = source::ByteSpan{first.offset, (last.end() + 1) - first.offset};
        for (const frontend::Clause& clause : law.clauses) {
            region.clauses.push_back(&clause);
        }
        regions.push_back(std::move(region));
    }

    for (const frontend::VerifiedFunction& verified : syntax.verified_functions) {
        if (verified.clauses.empty()) {
            continue;
        }
        ClauseRegion region;
        region.declaration_column = line_start_column(text, verified.keyword.offset);
        region.span = verified.clause_region;
        for (const frontend::Clause& clause : verified.clauses) {
            region.clauses.push_back(&clause);
        }
        regions.push_back(std::move(region));
    }

    for (const frontend::LoopSpecification& loop : syntax.loops) {
        if (loop.invariants.empty()) {
            continue;
        }
        ClauseRegion region;
        region.declaration_column = line_start_column(text, loop.keyword.offset);
        region.span = loop.clause_region;
        for (const frontend::Clause& clause : loop.invariants) {
            region.clauses.push_back(&clause);
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

std::vector<ProofRegion> collect_proof_regions(std::string_view text, const frontend::Syntax& syntax) {
    std::vector<ProofRegion> regions;
    for (const frontend::ProofDeclaration& proof : syntax.proofs) {
        if (proof.proves_keyword.length == 0) {
            continue;
        }
        ProofRegion region;
        region.declaration_column = line_start_column(text, proof.range.span.offset);
        region.span =
            source::ByteSpan{proof.proves_keyword.offset, (proof.proposition.end() + 1) - proof.proves_keyword.offset};
        region.proposition = proof.proposition;
        region.location = proof.proposition_location;
        regions.push_back(region);
    }
    return regions;
}

std::string canonical_proves_block(std::string_view text, const ProofRegion& region) {
    const std::string indent(region.declaration_column + kIndentWidth, ' ');
    std::string block;
    block += '\n';
    block += indent;
    block += "proves(";
    block += text.substr(region.proposition.offset, region.proposition.length);
    block += ')';
    return block;
}

bool is_horizontal_or_newline(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
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
std::optional<FormatEdit> make_clause_edit(std::string_view text, const ClauseRegion& region) {
    const source::ByteSpan widened = widen_over_whitespace(text, region.span);
    std::string block = canonical_clause_block(text, region.clauses, region.declaration_column);
    block += separator_after(text, widened.end(), region.declaration_column);
    if (text.substr(widened.offset, widened.length) == block) {
        return std::nullopt;
    }
    return FormatEdit{widened, std::move(block)};
}

std::optional<FormatEdit> make_proves_edit(std::string_view text, const ProofRegion& region) {
    const source::ByteSpan widened = widen_over_whitespace(text, region.span);
    std::string block = canonical_proves_block(text, region);
    block += separator_after(text, widened.end(), region.declaration_column);
    if (text.substr(widened.offset, widened.length) == block) {
        return std::nullopt;
    }
    return FormatEdit{widened, std::move(block)};
}

bool spans_overlap(source::ByteSpan lhs, source::ByteSpan rhs) {
    return lhs.offset < rhs.end() && rhs.offset < lhs.end();
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
// block back onto one line, since a bare `invariant(...)`/`expects(...)` call
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
    const frontend::Syntax syntax = frontend::recognize(stream, engine);

    const std::vector<ClauseRegion> all_regions = collect_regions(request.text, syntax);
    const std::vector<ProofRegion> all_proof_regions = collect_proof_regions(request.text, syntax);

    const bool whole_document = ranges.empty();
    auto overlaps_request = [&](source::ByteSpan span) {
        if (whole_document) {
            return true;
        }
        return std::ranges::any_of(ranges, [&](source::ByteSpan range) { return spans_overlap(range, span); });
    };

    std::vector<FormatEdit> edits;
    std::vector<source::ByteSpan> cppl_spans; // excluded from the ordinary-C++ line ranges below

    for (const ClauseRegion& region : all_regions) {
        if (!overlaps_request(region.span)) {
            continue;
        }
        if (std::optional<FormatEdit> edit = make_clause_edit(request.text, region); edit.has_value()) {
            edits.push_back(std::move(*edit));
        }
        cppl_spans.push_back(widen_over_whitespace(request.text, region.span));
    }
    for (const ProofRegion& region : all_proof_regions) {
        if (!overlaps_request(region.span)) {
            continue;
        }
        if (std::optional<FormatEdit> edit = make_proves_edit(request.text, region); edit.has_value()) {
            edits.push_back(std::move(*edit));
        }
        cppl_spans.push_back(widen_over_whitespace(request.text, region.span));
    }

    // The line ranges to ask clang-format to look at: the requested range(s),
    // or the whole document. This deliberately does NOT carve out the lines a
    // C++L clause above already claimed: clang-format is allowed to have an
    // opinion about a boundary a clause touches (e.g. the space between a
    // proof's closing ')' and its body's '{' on the same physical line,
    // 'proves(p) { refl; }' - verified directly, clang-format wants to expand
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

    const std::filesystem::path virtual_path(request.virtual_path);
    std::string stem = virtual_path.filename().string();
    if (stem.empty()) {
        stem = "buffer.cpp";
    }
    const std::string style_config =
        request.style_config.empty() ? std::string{CPPL_REPO_CLANG_FORMAT_CONFIG} : request.style_config;

    std::vector<FormatEdit> ordinary_edits = format_ordinary_cpp_lines(
        request.text, line_ranges, cppl_spans, request.clang_format, style_config, stem, result.diagnostics);
    edits.insert(edits.end(), std::make_move_iterator(ordinary_edits.begin()),
                 std::make_move_iterator(ordinary_edits.end()));

    std::ranges::sort(edits,
                      [](const FormatEdit& lhs, const FormatEdit& rhs) { return lhs.span.offset < rhs.span.offset; });

    result.ok = true;
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

FormatResult format_on_type(const FormatRequest& request, std::size_t position, const std::string& trigger_character) {
    static_cast<void>(trigger_character);

    const frontend::TokenStream stream =
        frontend::lex(request.text, request.virtual_path.empty() ? "buffer.cpp" : request.virtual_path);
    diagnostics::Engine engine;
    const frontend::Syntax syntax = frontend::recognize(stream, engine);

    for (const ClauseRegion& region : collect_regions(request.text, syntax)) {
        if (position >= region.span.offset && position <= region.span.end()) {
            return format_ranges(request, {region.span});
        }
    }
    for (const ProofRegion& region : collect_proof_regions(request.text, syntax)) {
        if (position >= region.span.offset && position <= region.span.end()) {
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

// No whitespace between a C++L clause keyword and its '(': `expects` and
// friends are not C++ control statements, so this repo's own
// SpaceBeforeParens: ControlStatements rule was never meant to apply to them.
bool has_canonical_spacing(std::string_view text, source::ByteSpan keyword) {
    return keyword.end() < text.size() && text[keyword.end()] == '(';
}

} // namespace

// Reuses exactly the region detection the formatting engine uses: a style
// violation is nothing more than "this clause's actual column differs from
// the column the engine would place it at" (ARCHITECTURE.md: one engine, not
// a second, drifting notion of what canonical means).
std::vector<diagnostics::Diagnostic> check_style(const frontend::TokenStream& stream, const frontend::Syntax& syntax) {
    std::vector<diagnostics::Diagnostic> out;
    const std::string_view text = stream.text();

    for (const ClauseRegion& region : collect_regions(text, syntax)) {
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
                    "'" + std::string(keyword_spelling(clause->kind)) + "' should not have whitespace before '('";
                diagnostic.location = clause->location;
                out.push_back(std::move(diagnostic));
            }
        }
    }

    for (const ProofRegion& region : collect_proof_regions(text, syntax)) {
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
            diagnostic.message = "'proves' should not have whitespace before '('";
            diagnostic.location = region.location;
            out.push_back(std::move(diagnostic));
        }
    }

    return out;
}

} // namespace cppl::formatter
