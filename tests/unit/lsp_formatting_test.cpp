// Proves cppl-lsp's textDocument/formatting handler and the cppl::formatter
// engine it wraps produce byte-identical output over the same input -- the
// LSP is a thin adapter (byte span -> LSP TextEdit) around one shared
// engine, not a second formatting implementation (compiler/formatter is the
// one canonical-formatting engine shared with the cppl-format CLI).

#include "cppl/formatter/format.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace cppl;

namespace {

std::string apply_formatter_edits(const std::string& text, const std::vector<formatter::FormatEdit>& edits) {
    std::vector<formatter::FormatEdit> sorted = edits;
    std::sort(sorted.begin(), sorted.end(), [](const formatter::FormatEdit& a, const formatter::FormatEdit& b) {
        return a.span.offset < b.span.offset;
    });
    std::string result;
    std::size_t cursor = 0;
    for (const formatter::FormatEdit& edit : sorted) {
        result.append(text, cursor, edit.span.offset - cursor);
        result += edit.replacement;
        cursor = edit.span.offset + edit.span.length;
    }
    result.append(text, cursor, text.size() - cursor);
    return result;
}

// Mirrors PositionMapper::position_to_byte_offset well enough for ASCII test
// input (no multi-byte UTF-16 surrogate pairs), so the LSP TextEdit results
// can be re-applied against the ORIGINAL text the same way apply_formatter_edits
// does for FormatEdits, without duplicating cppl::lsp::PositionMapper.
std::string apply_text_edits(const std::string& text, std::vector<lsp::TextEdit> edits) {
    std::vector<std::size_t> line_starts{0};
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            line_starts.push_back(i + 1);
        }
    }
    auto to_offset = [&](const lsp::Position& pos) -> std::size_t {
        const std::size_t line_start = line_starts.at(pos.line);
        return line_start + pos.character;
    };

    std::sort(edits.begin(), edits.end(), [&](const lsp::TextEdit& a, const lsp::TextEdit& b) {
        return to_offset(a.range.start) < to_offset(b.range.start);
    });

    std::string result;
    std::size_t cursor = 0;
    for (const lsp::TextEdit& edit : edits) {
        const std::size_t start = to_offset(edit.range.start);
        const std::size_t end = to_offset(edit.range.end);
        result.append(text, cursor, start - cursor);
        result += edit.newText;
        cursor = end;
    }
    result.append(text, cursor, text.size() - cursor);
    return result;
}

} // namespace

CPPL_TEST(lsp_document_formatting_matches_the_shared_engine_byte_for_byte) {
    const std::string text = "law bounded(unsigned x) expects(x < 10u) ensures(x + 1u <= 10u);\n"
                             "verified int f(int x) expects(x >= 0) ensures(result >= 0) {\n"
                             "    unsigned i = 0u;\n"
                             "    while (i < 3u) invariant(i <= 3u) { ++i; }\n"
                             "    return x;\n"
                             "}\n"
                             "type NonNegative = int where(self >= 0);\n";

    formatter::FormatRequest request;
    request.text = text;
    const formatter::FormatResult engine_result = formatter::format_document(request);
    CPPL_CHECK(engine_result.ok);
    const std::string engine_output = apply_formatter_edits(text, engine_result.edits);

    lsp::Server server;
    lsp::TextDocumentItem item;
    item.uri = "file:///lsp_formatting_test.cpp";
    item.text = text;
    item.version = 1;
    server.text_document_did_open(item);

    lsp::TextDocumentIdentifier id;
    id.uri = item.uri;
    const std::optional<std::vector<lsp::TextEdit>> lsp_edits = server.text_document_formatting(id);
    CPPL_CHECK(lsp_edits.has_value());
    const std::string lsp_output = apply_text_edits(text, *lsp_edits);

    CPPL_CHECK_EQ(lsp_output, engine_output);
}

CPPL_TEST(lsp_range_formatting_matches_the_shared_engine_over_the_same_range) {
    const std::string text = "verified int f(int x) ensures(result >= 0) {\n"
                             "    int   y   =   x;\n"
                             "    return y;\n"
                             "}\n";
    const std::size_t line_pos = text.find("int   y");
    const std::size_t line_end = text.find('\n', line_pos);

    formatter::FormatRequest request;
    request.text = text;
    const formatter::FormatResult engine_result =
        formatter::format_ranges(request, {source::ByteSpan{line_pos, line_end - line_pos}});
    CPPL_CHECK(engine_result.ok);
    const std::string engine_output = apply_formatter_edits(text, engine_result.edits);

    lsp::Server server;
    lsp::TextDocumentItem item;
    item.uri = "file:///lsp_range_formatting_test.cpp";
    item.text = text;
    item.version = 1;
    server.text_document_did_open(item);

    // Byte offsets are ASCII-only here, so line/character map 1:1 onto the
    // same offsets PositionMapper would compute.
    std::size_t line = 0;
    std::size_t column = 0;
    for (std::size_t i = 0; i < line_pos; ++i) {
        if (text[i] == '\n') {
            ++line;
            column = 0;
        } else {
            ++column;
        }
    }
    lsp::Range range;
    range.start.line = static_cast<std::uint32_t>(line);
    range.start.character = static_cast<std::uint32_t>(column);
    range.end.line = static_cast<std::uint32_t>(line);
    range.end.character = static_cast<std::uint32_t>(column + (line_end - line_pos));

    lsp::TextDocumentIdentifier id;
    id.uri = item.uri;
    const std::optional<std::vector<lsp::TextEdit>> lsp_edits = server.text_document_range_formatting(id, range);
    CPPL_CHECK(lsp_edits.has_value());
    const std::string lsp_output = apply_text_edits(text, *lsp_edits);

    CPPL_CHECK_EQ(lsp_output, engine_output);
}
