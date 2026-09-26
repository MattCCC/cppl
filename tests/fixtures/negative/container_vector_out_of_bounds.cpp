// SPEC: STDMODEL-012
// A subscript of a vector owes `i < v.size()`, and a vector of `n` elements
// does not bound an arbitrary `i`. Accepted twin: `vector_write_read`.
#include <cstddef>
#include <vector>

verified unsigned vector_unguarded(std::size_t n, std::size_t i)
    ensures (result == 5u)
{
    std::vector<unsigned> v(n);
    v[i] = 5u;
    return v[i];
}

int main() {
    return 0;
}
