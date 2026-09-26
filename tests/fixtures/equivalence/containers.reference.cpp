// Ordinary C++: `containers.cpp` erased by hand as SPEC.md 36 and Annex M say it
// erases. The refinement is the alias of its base type, and every contract,
// capability, loop invariant and measure is gone; each container is the
// standard library's own type, used exactly as written.
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <vector>

using Positive = unsigned;

std::size_t fill(std::size_t n) {
    std::vector<Positive> v;
    std::size_t i = 0ul;
    while (i < n) {
        v.push_back(2u);
        ++i;
    }
    return v.size();
}

unsigned sum(std::span<const unsigned> in) {
    unsigned total = 0u;
    std::size_t i = 0ul;
    while (i < in.size()) {
        total += in[i];
        ++i;
    }
    return total;
}

void scale(std::span<unsigned> out, unsigned factor) {
    std::size_t i = 0ul;
    while (i < out.size()) {
        out[i] = out[i] * factor;
        ++i;
    }
}

std::size_t rebuild(std::vector<unsigned>& v) {
    v.clear();
    v.push_back(4u);
    v.push_back(5u);
    return v.size();
}

std::size_t text(const std::string& suffix) {
    std::string s = "abc";
    s.push_back('d');
    s += suffix;
    return s.size();
}

unsigned pick(std::array<unsigned, 3> a, std::size_t i) {
    a[i] = a[i] + 1u;
    return a[i];
}

std::size_t moved() {
    std::vector<unsigned> v{1u, 2u, 3u};
    std::vector<unsigned> w = std::move(v);
    return w.size();
}

int main() {
    std::vector<unsigned> values{1u, 2u, 3u};
    scale(values, 3u);
    const unsigned total = sum(values);
    const std::size_t rebuilt = rebuild(values);
    std::printf("%zu %u %zu %zu %u %zu %u\n", fill(5ul), total, rebuilt, text("xy"), pick({7u, 8u, 9u}, 1ul), moved(),
                values[1]);
    return 0;
}
