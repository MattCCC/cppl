// SPEC: STDMODEL-015, STDMODEL-021
// A span over a vector moved from: the storage it viewed now belongs to another
// container, and nothing about the moved-from one is known. Accepted twin:
// `vector_moved`.
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

verified unsigned span_outlives_move()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<const unsigned> s(v);
    std::vector<unsigned> w = std::move(v);
    if (0ul < s.size()) {
        return s[0];
    }
    return 0u;
}

int main() {
    return 0;
}
