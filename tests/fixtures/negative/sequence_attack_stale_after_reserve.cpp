// SPEC: STDMODEL-015, STDMODEL-025
// `reserve` keeps the length but may replace the storage, so a span formed
// before it views a dead generation. Accepted twin: `read_before_reserve` in
// sequence_attacks.cpp, which uses the span only before the call.
#include <cstddef>
#include <span>
#include <vector>

verified unsigned stale_after_reserve(std::size_t i)
    ensures (result == 6u)
{
    std::vector<unsigned> v{1u, 2u, 3u};
    std::span<unsigned> s(v);
    v.reserve(100ul);
    if (i < s.size()) {
        s[i] = 6u;
        return v[i];
    }
    return 6u;
}

int main() {
    return 0;
}
