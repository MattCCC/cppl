// SPEC: STDMODEL-021
// A moved-from vector is valid but unspecified: its length before the move is
// no longer known. Accepted twin: `vector_moved`, which reads the destination.
#include <cstddef>
#include <utility>
#include <vector>

verified std::size_t moved_from()
    ensures (result == 2ul)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w = std::move(v);
    return v.size();
}

int main() {
    return 0;
}
