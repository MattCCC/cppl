// SPEC: STDMODEL-021
// Nothing about a moved-from vector's length survives the move, so no index of
// it is in bounds. Accepted twin: `moved_to_element` in sequence_attacks.cpp,
// which reads the destination.
#include <cstddef>
#include <utility>
#include <vector>

verified unsigned moved_from_element()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w = std::move(v);
    return v[0];
}

int main() {
    return 0;
}
