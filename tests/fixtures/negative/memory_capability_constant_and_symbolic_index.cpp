// SPEC: VERIFIED-039, VERIFIED-040
// Through one pointer, `p[j]` may be `p[1]`: a symbolic index selects an element
// this implementation cannot decide, so a write through it reaches every
// element of the same pointee, at a constant index too, as it does for an
// array. Called with j == 1, this returns 5. Accepted twin:
// `constant_index_kept` in memory_capabilities.cpp.
#include <cstddef>

verified unsigned constant_then_symbolic(unsigned* p, std::size_t n, std::size_t j)
    expects (readable(p, n) && writable(p, n))
    ensures (result == 1u)
{
    if (2ul < n && j < n) {
        p[1] = 1u;
        p[j] = 5u;
        return p[1];
    }
    return 1u;
}

verified unsigned symbolic_then_constant(unsigned* p, std::size_t n, std::size_t j)
    expects (readable(p, n) && writable(p, n))
    ensures (result == 1u)
{
    if (0ul < n && j < n) {
        p[j] = 1u;
        p[0] = 5u;
        return p[j];
    }
    return 1u;
}

int main() {
    return 0;
}
