// SPEC: STDMODEL-014, STDMODEL-015
// A span over a vector, used after `push_back` may have reallocated what it
// views. Accepted twin: `span_local`, which uses it before any mutation.
#include <cstddef>
#include <span>
#include <vector>

verified unsigned stale_span(std::size_t i)
    ensures (result == 6u)
{
    std::vector<unsigned> v{1u, 2u, 3u};
    std::span<unsigned> s(v);
    v.push_back(4u);
    if (i < s.size()) {
        s[i] = 6u;
        return v[i];
    }
    return 6u;
}

int main() {
    return 0;
}
