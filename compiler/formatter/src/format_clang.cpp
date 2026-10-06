// Ordinary C++ and the expressions inside C++L syntax, formatted by
// clang-format, whose replacements are read back here.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/driver/process.hpp"
#include "cppl/driver/scratch.hpp"
#include "cppl/formatter/canonical_style.hpp"
#include "cppl/formatter/format.hpp"
#include "cppl/source/location.hpp"
#include "format_detail.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef CPPL_DEFAULT_CLANG_FORMAT
#define CPPL_DEFAULT_CLANG_FORMAT "clang-format"
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

// The `-style` argument clang-format is run with in `scratch`: the file a
// request names, or the canonical C++L style, which is built into the formatter
// so that an installed `cppl-format` or `cppl-lsp` formats the same way as the
// build tree does, with no file of the source tree to find (`.clang-format`,
// embedded by compiler/formatter/CMakeLists.txt). Nothing when that style
// cannot be written.
std::optional<std::string> style_argument(const std::string& style_config, const std::filesystem::path& scratch) {
    if (!style_config.empty()) {
        return "-style=file:" + style_config;
    }
    const std::filesystem::path canonical = scratch / "canonical.clang-format";
    if (!driver::write_scratch_file(canonical, kCanonicalStyle)) {
        return std::nullopt;
    }
    return "-style=file:" + canonical.string();
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

} // namespace

namespace detail {

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

    const std::optional<std::string> style = style_argument(style_config, scratch.path());
    if (!style.has_value()) {
        report(out, diagnostics::Severity::Error, diagnostics::Category::Internal,
               "could not write the canonical style for '" + stem + "'");
        return {};
    }
    const std::string tool = clang_format.empty() ? std::string{CPPL_DEFAULT_CLANG_FORMAT} : clang_format;

    const std::vector<std::string> arguments{*style, "--output-replacements-xml", source_path.string()};

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

    const std::optional<std::string> style = style_argument(style_config, scratch.path());
    if (!style.has_value()) {
        report(out, diagnostics::Severity::Error, diagnostics::Category::Internal,
               "could not write the canonical style for '" + stem + "'");
        return {};
    }
    const std::string tool = clang_format.empty() ? std::string{CPPL_DEFAULT_CLANG_FORMAT} : clang_format;

    std::vector<std::string> arguments{*style, "--output-replacements-xml"};
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

} // namespace detail

} // namespace cppl::formatter
