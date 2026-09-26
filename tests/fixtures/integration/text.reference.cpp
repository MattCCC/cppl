// integration/text.hpp and text.cpp with every C++L construct erased by hand:
// what a C++ programmer writes without C++L. tests/e2e/integration_ledger.sh
// compiles this with a C++ compiler alone and requires the same code as
// text.cpp compiled by cppl, whatever interface it writes (SPEC.md ERASE-002,
// ABI-001).
#include <cstddef>
#include <span>

std::size_t skip_spaces(std::span<const char> in, std::size_t at);
std::size_t digits_end(std::span<const char> in, std::size_t at);
std::size_t line_end(std::span<const char> in, std::size_t at);
long long read_number(std::span<const char> in, std::size_t from, std::size_t to);
std::size_t line_limit();

std::size_t skip_spaces(std::span<const char> in, std::size_t at) {
    std::size_t i = at;
    while (i < in.size()) {
        const char c = in[i];
        if (c != ' ') {
            return i;
        }
        ++i;
    }
    return i;
}

std::size_t digits_end(std::span<const char> in, std::size_t at) {
    std::size_t i = at;
    while (i < in.size()) {
        const char c = in[i];
        if (c < '0' || c > '9') {
            return i;
        }
        ++i;
    }
    return i;
}

std::size_t line_end(std::span<const char> in, std::size_t at) {
    std::size_t i = at;
    while (i < in.size()) {
        const char c = in[i];
        if (c == '\n') {
            return i;
        }
        ++i;
    }
    return i;
}

long long read_number(std::span<const char> in, std::size_t from, std::size_t to) {
    if (from == to) {
        return -1ll;
    }
    long long value = 0ll;
    std::size_t i = from;
    while (i < to) {
        const char c = in[i];
        if (c < '0' || c > '9') {
            return -1ll;
        }
        if (value > 99999ll) {
            return -1ll;
        }
        value = value * 10ll + (c - '0');
        ++i;
    }
    return value;
}

std::size_t configured_line_limit();

std::size_t line_limit() {
    std::size_t limit = 0ul;
    {
        limit = configured_line_limit();
    }
    if (limit < 1ul || limit > 4096ul) {
        return 4096ul;
    }
    return limit;
}

std::size_t configured_line_limit() {
    return 256ul;
}
