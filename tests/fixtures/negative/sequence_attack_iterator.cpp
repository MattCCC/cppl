// SPEC: STDMODEL-019
// Iterators are not modeled; an element is reached only by a bounded
// `operator[]`. Accepted twin: `first_by_index` in sequence_attacks.cpp.
#include <cstddef>
#include <vector>

verified unsigned first_through_iterator()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    auto it = v.begin();
    return *it;
}

int main() {
    return 0;
}
