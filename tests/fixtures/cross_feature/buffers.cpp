// Proves the cursor `buffers.hpp` declares.
#include "buffers.hpp"

unsigned Cursor::peek(std::span<const unsigned> in) const {
    return in[at];
}

std::size_t Cursor::emit(std::vector<unsigned>& out, unsigned value) const {
    out.push_back(value);
    return out.size();
}

std::size_t Cursor::length_of(const std::vector<unsigned>& in) const {
    return in.size();
}

char Cursor::character(const std::string& text) const {
    return text[at];
}

long long Cursor::moved(long long delta) const {
    return static_cast<long long>(at) + delta;
}

unsafe std::size_t read_setting();

std::size_t Cursor::setting() const {
    std::size_t value = 0ul;
    unsafe {
        value = read_setting();
    }
    if (value > 4096ul) {
        return 4096ul;
    }
    return value;
}

verified std::size_t suffixed_length(std::string text)
    ensures (result == text.size() + 1ul)
{
    text.push_back('!');
    return text.size();
}

unsafe std::size_t read_setting() {
    return 64ul;
}
