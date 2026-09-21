#include "cppl/lsp/position.hpp"

#include <algorithm>
#include <cassert>

namespace cppl::lsp {

namespace {

// The byte length of the UTF-8 sequence starting at `lead`, and how many
// UTF-16 code units it decodes to (2 only for a 4-byte sequence, which
// encodes as a UTF-16 surrogate pair; 1 otherwise, including the fallback for
// a lead byte that is not valid UTF-8, which fail-open in this file has
// always treated as a single code unit).
struct Utf8Step {
    std::size_t bytes = 1;
    std::uint32_t utf16_units = 1;
};

[[nodiscard]] Utf8Step utf8_step(unsigned char lead) {
    if ((lead & 0x80) == 0) {
        return {1, 1};
    }
    if ((lead & 0xE0) == 0xC0) {
        return {2, 1};
    }
    if ((lead & 0xF0) == 0xE0) {
        return {3, 1};
    }
    if ((lead & 0xF8) == 0xF0) {
        return {4, 2};
    }
    return {1, 1};
}

} // namespace

PositionMapper::PositionMapper(std::string_view text) : text_(text) {
    // Build line start index
    line_starts_.push_back(0);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            line_starts_.push_back(i + 1);
        }
    }
}

Position PositionMapper::byte_offset_to_position(std::size_t offset) const {
    // Find the line containing this offset
    auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
    if (it == line_starts_.begin()) {
        return Position{0, 0};
    }
    --it;

    std::uint32_t line = static_cast<std::uint32_t>(std::distance(line_starts_.begin(), it));
    std::size_t line_start = *it;

    // Count UTF-16 code units from line start to offset
    std::uint32_t character = count_utf16_code_units(text_, line_start, std::min(offset, text_.size()));

    return Position{line, character};
}

std::size_t PositionMapper::position_to_byte_offset(const Position& pos) const {
    if (pos.line >= line_starts_.size()) {
        return text_.size();
    }

    std::size_t line_start = line_starts_[pos.line];
    std::size_t line_end = (pos.line + 1 < line_starts_.size()) ? line_starts_[pos.line + 1] - 1 : text_.size();

    // Find byte offset corresponding to UTF-16 character position
    std::uint32_t utf16_count = 0;
    std::size_t byte_offset = line_start;

    while (byte_offset < line_end && utf16_count < pos.character) {
        const Utf8Step step = utf8_step(static_cast<unsigned char>(text_[byte_offset]));
        byte_offset += step.bytes;
        utf16_count += step.utf16_units;
    }

    return std::min(byte_offset, text_.size());
}

Position PositionMapper::source_location_to_position(const source::SourceLocation& loc) const {
    // SourceLocation uses 1-indexed lines and columns
    if (loc.line == 0) {
        return Position{0, 0};
    }
    return Position{loc.line - 1, loc.column > 0 ? loc.column - 1 : 0};
}

Range PositionMapper::byte_span_to_range(source::ByteSpan span) const {
    Position start = byte_offset_to_position(span.offset);
    Position end = byte_offset_to_position(span.end());
    return Range{start, end};
}

Range PositionMapper::source_range_to_range(const source::SourceRange& range) const {
    Position start = source_location_to_position(range.begin);
    // Use the byte span to calculate the end position
    Position end = byte_offset_to_position(range.span.end());
    return Range{start, end};
}

std::uint32_t count_utf16_code_units(std::string_view text, std::size_t start, std::size_t end) {
    std::uint32_t count = 0;
    for (std::size_t i = start; i < end && i < text.size();) {
        const Utf8Step step = utf8_step(static_cast<unsigned char>(text[i]));
        i += step.bytes;
        count += step.utf16_units;
    }
    return count;
}

} // namespace cppl::lsp
