// UTF-16 position mapping for LSP must handle Unicode correctly.

#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/source/location.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

using namespace cppl::lsp;
using namespace cppl::source;

CPPL_TEST(byte_offset_to_position_ascii) {
    std::string text = "hello\nworld\n";
    PositionMapper mapper(text);

    Position pos = mapper.byte_offset_to_position(0);
    CPPL_CHECK_EQ(pos.line, 0u);
    CPPL_CHECK_EQ(pos.character, 0u);

    pos = mapper.byte_offset_to_position(5); // newline
    CPPL_CHECK_EQ(pos.line, 0u);
    CPPL_CHECK_EQ(pos.character, 5u);

    pos = mapper.byte_offset_to_position(6); // 'w'
    CPPL_CHECK_EQ(pos.line, 1u);
    CPPL_CHECK_EQ(pos.character, 0u);

    pos = mapper.byte_offset_to_position(11); // second newline
    CPPL_CHECK_EQ(pos.line, 1u);
    CPPL_CHECK_EQ(pos.character, 5u);
}

CPPL_TEST(byte_offset_to_position_with_unicode) {
    // "café" = c(1) a(1) f(1) é(2 bytes in UTF-8, 1 UTF-16 code unit)
    std::string text = "café\n";
    PositionMapper mapper(text);

    Position pos = mapper.byte_offset_to_position(0);
    CPPL_CHECK_EQ(pos.line, 0u);
    CPPL_CHECK_EQ(pos.character, 0u);

    pos = mapper.byte_offset_to_position(3); // just before 'é'
    CPPL_CHECK_EQ(pos.line, 0u);
    CPPL_CHECK_EQ(pos.character, 3u);

    pos = mapper.byte_offset_to_position(5); // after 'é' (2 bytes), at newline
    CPPL_CHECK_EQ(pos.line, 0u);
    CPPL_CHECK_EQ(pos.character, 4u); // 4 UTF-16 code units
}

CPPL_TEST(byte_offset_to_position_with_emoji) {
    // "😀" is 4 bytes in UTF-8, 2 UTF-16 code units (surrogate pair)
    std::string text = "a😀b\n";
    PositionMapper mapper(text);

    Position pos = mapper.byte_offset_to_position(0); // 'a'
    CPPL_CHECK_EQ(pos.line, 0u);
    CPPL_CHECK_EQ(pos.character, 0u);

    pos = mapper.byte_offset_to_position(1); // start of emoji
    CPPL_CHECK_EQ(pos.line, 0u);
    CPPL_CHECK_EQ(pos.character, 1u);

    pos = mapper.byte_offset_to_position(5); // 'b' (after 4-byte emoji)
    CPPL_CHECK_EQ(pos.line, 0u);
    CPPL_CHECK_EQ(pos.character, 3u); // a(1) + emoji(2) = 3 UTF-16 code units

    pos = mapper.byte_offset_to_position(6); // newline
    CPPL_CHECK_EQ(pos.line, 0u);
    CPPL_CHECK_EQ(pos.character, 4u);
}

CPPL_TEST(position_to_byte_offset_ascii) {
    std::string text = "hello\nworld\n";
    PositionMapper mapper(text);

    std::size_t offset = mapper.position_to_byte_offset(Position{0, 0});
    CPPL_CHECK_EQ(offset, 0u);

    offset = mapper.position_to_byte_offset(Position{0, 5});
    CPPL_CHECK_EQ(offset, 5u);

    offset = mapper.position_to_byte_offset(Position{1, 0});
    CPPL_CHECK_EQ(offset, 6u);

    offset = mapper.position_to_byte_offset(Position{1, 5});
    CPPL_CHECK_EQ(offset, 11u);
}

CPPL_TEST(position_to_byte_offset_with_unicode) {
    std::string text = "café\n";
    PositionMapper mapper(text);

    std::size_t offset = mapper.position_to_byte_offset(Position{0, 4}); // after 'é'
    CPPL_CHECK_EQ(offset, 5u);                                           // 'c' 'a' 'f' 'é'(2 bytes) = 5 bytes
}

CPPL_TEST(position_to_byte_offset_with_emoji) {
    std::string text = "a😀b\n";
    PositionMapper mapper(text);

    std::size_t offset = mapper.position_to_byte_offset(Position{0, 1}); // after 'a'
    CPPL_CHECK_EQ(offset, 1u);

    offset = mapper.position_to_byte_offset(Position{0, 3}); // after emoji (2 UTF-16 units)
    CPPL_CHECK_EQ(offset, 5u);                               // a(1) + emoji(4) = 5 bytes

    offset = mapper.position_to_byte_offset(Position{0, 4}); // after 'b'
    CPPL_CHECK_EQ(offset, 6u);
}

CPPL_TEST(byte_span_to_range) {
    std::string text = "line1\nline2\n";
    PositionMapper mapper(text);

    ByteSpan span{0, 5}; // "line1"
    Range range = mapper.byte_span_to_range(span);
    CPPL_CHECK_EQ(range.start.line, 0u);
    CPPL_CHECK_EQ(range.start.character, 0u);
    CPPL_CHECK_EQ(range.end.line, 0u);
    CPPL_CHECK_EQ(range.end.character, 5u);

    span = ByteSpan{6, 5}; // "line2"
    range = mapper.byte_span_to_range(span);
    CPPL_CHECK_EQ(range.start.line, 1u);
    CPPL_CHECK_EQ(range.start.character, 0u);
    CPPL_CHECK_EQ(range.end.line, 1u);
    CPPL_CHECK_EQ(range.end.character, 5u);
}

CPPL_TEST(source_location_to_position) {
    std::string text = "line1\nline2\n";
    PositionMapper mapper(text);

    // SourceLocation uses 1-indexed lines and columns
    SourceLocation loc{"test.cpp", 1, 1};
    Position pos = mapper.source_location_to_position(loc);
    CPPL_CHECK_EQ(pos.line, 0u);
    CPPL_CHECK_EQ(pos.character, 0u);

    loc = SourceLocation{"test.cpp", 2, 3};
    pos = mapper.source_location_to_position(loc);
    CPPL_CHECK_EQ(pos.line, 1u);
    CPPL_CHECK_EQ(pos.character, 2u);
}

CPPL_TEST(a_source_location_column_counts_bytes_and_a_position_utf16_units) {
    // The compiler's columns are byte columns. `é` is two bytes and one UTF-16
    // unit; `😀` is four bytes and two units.
    std::string text = "x\n\xC3\xA9 = y;\n\xF0\x9F\x98\x80z;\n";
    PositionMapper mapper(text);

    Position pos = mapper.source_location_to_position(SourceLocation{"test.cpp", 2, 4}); // '='
    CPPL_CHECK_EQ(pos.line, 1u);
    CPPL_CHECK_EQ(pos.character, 2u);

    pos = mapper.source_location_to_position(SourceLocation{"test.cpp", 3, 5}); // 'z'
    CPPL_CHECK_EQ(pos.line, 2u);
    CPPL_CHECK_EQ(pos.character, 2u);
}

CPPL_TEST(a_source_location_past_its_line_or_the_text_keeps_its_distance) {
    std::string text = "\xC3\xA9;\n";
    PositionMapper mapper(text);

    // Two bytes past the end of `é;`: two characters past its two.
    Position pos = mapper.source_location_to_position(SourceLocation{"test.cpp", 1, 6});
    CPPL_CHECK_EQ(pos.line, 0u);
    CPPL_CHECK_EQ(pos.character, 4u);

    // A line the text does not have is passed through as written.
    pos = mapper.source_location_to_position(SourceLocation{"test.cpp", 9, 7});
    CPPL_CHECK_EQ(pos.line, 8u);
    CPPL_CHECK_EQ(pos.character, 6u);
}

CPPL_TEST(count_utf16_code_units_ascii) {
    std::string text = "hello";
    std::uint32_t count = count_utf16_code_units(text, 0, 5);
    CPPL_CHECK_EQ(count, 5u);
}

CPPL_TEST(count_utf16_code_units_unicode) {
    std::string text = "café"; // 5 bytes, 4 UTF-16 code units
    std::uint32_t count = count_utf16_code_units(text, 0, 5);
    CPPL_CHECK_EQ(count, 4u);
}

CPPL_TEST(count_utf16_code_units_emoji) {
    std::string text = "😀"; // 4 bytes, 2 UTF-16 code units
    std::uint32_t count = count_utf16_code_units(text, 0, 4);
    CPPL_CHECK_EQ(count, 2u);
}

CPPL_TEST(multiline_unicode) {
    std::string text = "first\nсекунд\nthird\n"; // Cyrillic "секунд"
    PositionMapper mapper(text);

    Position pos = mapper.byte_offset_to_position(6); // start of second line
    CPPL_CHECK_EQ(pos.line, 1u);
    CPPL_CHECK_EQ(pos.character, 0u);

    pos = mapper.byte_offset_to_position(18); // end of Cyrillic word (6 chars * 2 bytes each = 12 bytes)
    CPPL_CHECK_EQ(pos.line, 1u);
    CPPL_CHECK_EQ(pos.character, 6u); // 6 Cyrillic characters
}
