#include "cppl/source/digest.hpp"

#include <algorithm>
#include <cstring>

namespace cppl::source {

namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

constexpr std::uint32_t rotate_right(std::uint32_t value, std::uint32_t amount) {
    return (value >> amount) | (value << (32u - amount));
}

} // namespace

Hasher::Hasher() {
    state_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
}

void Hasher::compress(const std::uint8_t block[64]) {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t index = 0; index < 16; ++index) {
        schedule[index] = (static_cast<std::uint32_t>(block[index * 4]) << 24) |
                          (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16) |
                          (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8) |
                          static_cast<std::uint32_t>(block[index * 4 + 3]);
    }
    for (std::size_t index = 16; index < 64; ++index) {
        const std::uint32_t s0 = rotate_right(schedule[index - 15], 7) ^ rotate_right(schedule[index - 15], 18) ^
                                 (schedule[index - 15] >> 3);
        const std::uint32_t s1 =
            rotate_right(schedule[index - 2], 17) ^ rotate_right(schedule[index - 2], 19) ^ (schedule[index - 2] >> 10);
        schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t index = 0; index < 64; ++index) {
        const std::uint32_t S1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + S1 + choice + kRoundConstants[index] + schedule[index];
        const std::uint32_t S0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = S0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Hasher::update(const void* data, std::size_t length) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    total_bits_ += static_cast<std::uint64_t>(length) * 8u;

    while (length > 0) {
        const std::size_t taken = std::min(length, buffer_.size() - buffered_);
        std::memcpy(buffer_.data() + buffered_, bytes, taken);
        buffered_ += taken;
        bytes += taken;
        length -= taken;

        if (buffered_ == buffer_.size()) {
            compress(buffer_.data());
            buffered_ = 0;
        }
    }
}

void Hasher::update(std::string_view text) {
    update(text.data(), text.size());
}

void Hasher::update_field(std::string_view text) {
    update_u64(text.size());
    update(text.data(), text.size());
}

void Hasher::update_u64(std::uint64_t value) {
    std::array<std::uint8_t, 8> encoded{};
    for (std::size_t index = 0; index < 8; ++index) {
        encoded[index] = static_cast<std::uint8_t>((value >> (56u - index * 8u)) & 0xffu);
    }
    update(encoded.data(), encoded.size());
}

void Hasher::update_u8(std::uint8_t value) {
    update(&value, 1);
}

Digest Hasher::finish() {
    if (!finished_) {
        const std::uint64_t length_bits = total_bits_;
        update_u8(0x80);
        while (buffered_ != 56) {
            update_u8(0x00);
        }
        // The padding above advanced the counter; the length field must record
        // the message length before padding.
        std::array<std::uint8_t, 8> encoded{};
        for (std::size_t index = 0; index < 8; ++index) {
            encoded[index] = static_cast<std::uint8_t>((length_bits >> (56u - index * 8u)) & 0xffu);
        }
        std::memcpy(buffer_.data() + buffered_, encoded.data(), encoded.size());
        compress(buffer_.data());
        buffered_ = 0;
        finished_ = true;
    }

    Digest digest;
    for (std::size_t index = 0; index < 8; ++index) {
        digest.bytes[index * 4] = static_cast<std::uint8_t>((state_[index] >> 24) & 0xffu);
        digest.bytes[index * 4 + 1] = static_cast<std::uint8_t>((state_[index] >> 16) & 0xffu);
        digest.bytes[index * 4 + 2] = static_cast<std::uint8_t>((state_[index] >> 8) & 0xffu);
        digest.bytes[index * 4 + 3] = static_cast<std::uint8_t>(state_[index] & 0xffu);
    }
    return digest;
}

std::string Digest::to_hex() const {
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (std::uint8_t byte : bytes) {
        text.push_back(kHexDigits[byte >> 4]);
        text.push_back(kHexDigits[byte & 0x0fu]);
    }
    return text;
}

std::string Digest::to_short_hex(std::size_t characters) const {
    std::string text = to_hex();
    if (characters < text.size()) {
        text.resize(characters);
    }
    return text;
}

Digest hash_bytes(std::string_view text) {
    Hasher hasher;
    hasher.update(text);
    return hasher.finish();
}

} // namespace cppl::source
