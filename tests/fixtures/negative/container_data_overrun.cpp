// SPEC: STDMODEL-017
// A container's data pointer designates as many elements as it has, not one
// more. Accepted twin: `data_argument`.
#include <cstddef>
#include <vector>

verified unsigned clear_prefix(unsigned* p, std::size_t n)
    expects (writable(p, n))
    ensures (result == 0u)
{
    std::size_t i = 0ul;
    while (i < n)
        invariant (i <= n)
    {
        p[i] = 0u;
        ++i;
    }
    return 0u;
}

verified unsigned data_overrun()
    ensures (result == 0u)
{
    std::vector<unsigned> v{4u, 5u, 6u};
    return clear_prefix(v.data(), v.size() + 1ul);
}

int main() {
    return 0;
}
