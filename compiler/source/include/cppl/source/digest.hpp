#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cppl::source {

// A 256-bit content digest.
//
// Semantic identities in C++L are derived from content, never from memory
// addresses, iteration order or wall-clock time (ARCHITECTURE.md 12, 70).
struct Digest {
    std::array<std::uint8_t, 32> bytes{};

    [[nodiscard]] std::string to_hex() const;

    // The leading hex characters, for identifiers that appear in diagnostics.
    [[nodiscard]] std::string to_short_hex(std::size_t characters = 16) const;

    friend bool operator==(const Digest&, const Digest&) = default;
};

// SHA-256. Deterministic on every platform and independent of process state.
class Hasher {
  public:
    Hasher();

    void update(const void* data, std::size_t length);
    void update(std::string_view text);

    // Length-prefixed, so concatenating different field splits cannot produce
    // the same digest.
    void update_field(std::string_view text);
    void update_u64(std::uint64_t value);
    void update_u8(std::uint8_t value);

    [[nodiscard]] Digest finish();

  private:
    void compress(const std::uint8_t block[64]);

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t total_bits_ = 0;
    bool finished_ = false;
};

Digest hash_bytes(std::string_view text);

} // namespace cppl::source
