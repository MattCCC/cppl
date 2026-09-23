#pragma once

#include "cppl/lsp/protocol.hpp"
#include "cppl/source/location.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cppl::lsp {

// Utilities for converting between byte offsets, line/column positions, and LSP UTF-16 positions
class PositionMapper {
  public:
    explicit PositionMapper(std::string_view text);

    // Convert byte offset to line/column (both 0-indexed)
    [[nodiscard]] Position byte_offset_to_position(std::size_t offset) const;

    // Convert line/column (0-indexed) to byte offset
    [[nodiscard]] std::size_t position_to_byte_offset(const Position& pos) const;

    // Convert source::SourceLocation (1-indexed line, 1-indexed byte column) to
    // LSP Position (0-indexed line, UTF-16 character)
    [[nodiscard]] Position source_location_to_position(const source::SourceLocation& loc) const;

    // Convert source::ByteSpan to LSP Range
    [[nodiscard]] Range byte_span_to_range(source::ByteSpan span) const;


  private:
    std::string_view text_;
    std::vector<std::size_t> line_starts_; // byte offset of each line start
};

// Count UTF-16 code units from the start of the line to the given byte offset
[[nodiscard]] std::uint32_t count_utf16_code_units(std::string_view text, std::size_t start, std::size_t end);

} // namespace cppl::lsp
