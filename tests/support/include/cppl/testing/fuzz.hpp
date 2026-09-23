#pragma once

// Helpers shared by the fuzz targets in tests/fuzz (docs/CI.md, "Fuzzing").

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <source_location>
#include <span>
#include <string>

namespace cppl::testing::fuzz {

// A fuzz target's oracle. A property that does not hold is a crash, so
// libFuzzer keeps the input that broke it and the replay test fails on it.
inline void require(bool holds, const char* property,
                    const std::source_location where = std::source_location::current()) {
    if (!holds) {
        std::cerr << where.file_name() << ':' << where.line() << ": property does not hold: " << property << '\n';
        std::abort();
    }
}

// The input as text. Copied rather than reinterpreted, so no byte is read
// through a pointer of another type.
[[nodiscard]] inline std::string text(const std::span<const std::uint8_t> bytes) {
    std::string result(bytes.size(), '\0');
    std::ranges::transform(bytes, result.begin(), [](const std::uint8_t byte) { return static_cast<char>(byte); });
    return result;
}

} // namespace cppl::testing::fuzz
