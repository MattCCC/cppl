// SPEC: STDMODEL-015
// A loop that appends gives the vector a new generation at its head, so a span
// formed before the loop is stale after it, whether or not the loop ran.
// Accepted twin: `span_local`.
#include <cstddef>
#include <span>
#include <vector>

verified unsigned stale_after_loop(std::size_t n)
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    std::span<const unsigned> s(v);
    std::size_t i = 0ul;
    while (i < n)
        invariant (i <= n)
    {
        v.push_back(2u);
        ++i;
    }
    if (0ul < s.size()) {
        return s[0];
    }
    return 0u;
}

int main() {
    return 0;
}
