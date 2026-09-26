// SPEC: STDMODEL-020
// A subscript write into a refined vector owes the element's predicate.
// Accepted twin: `vector_refined`.
#include <cstddef>
#include <vector>

type Positive = unsigned where (self > 0u);

verified void write_zero(std::size_t i)
    ensures (true)
{
    std::vector<Positive> v{1u, 2u};
    if (i < v.size()) {
        v[i] = 0u;
    }
}

int main() {
    return 0;
}
