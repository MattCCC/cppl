#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace cppl::source {

// A half-open byte range inside one text buffer.
struct ByteSpan {
    std::size_t offset = 0;
    std::size_t length = 0;

    [[nodiscard]] std::size_t end() const noexcept { return offset + length; }

    friend bool operator==(const ByteSpan&, const ByteSpan&) = default;
};

// A position in original user source.
//
// After preprocessing, physical positions in the token buffer no longer match
// the files a developer edits. Every location carried through the compiler is
// therefore the presumed location: the file and line that the preprocessor's
// line markers attribute the text to.
struct SourceLocation {
    std::string file;
    std::uint32_t line = 0;
    std::uint32_t column = 0;

    [[nodiscard]] bool is_valid() const noexcept { return !file.empty() && line != 0; }

    friend bool operator==(const SourceLocation&, const SourceLocation&) = default;
};

// A construct's position in user source together with its extent in the buffer
// the frontend actually scanned.
struct SourceRange {
    SourceLocation begin;
    ByteSpan span;

    friend bool operator==(const SourceRange&, const SourceRange&) = default;
};

std::string describe(const SourceLocation& location);

}  // namespace cppl::source
