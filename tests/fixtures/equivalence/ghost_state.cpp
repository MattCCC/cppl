// Ghost state (SPEC.md 25, ERASE-011, Annex M).
//
// Erased, this unit must compile to exactly the code `ghost_state.reference.cpp`
// compiles to. Every ghost declaration leaves whole, its initializer with it,
// and every statement around it stays.
#include <cstdio>

pure unsigned twice(unsigned x) {
    return x + x;
}

verified unsigned count(unsigned n)
    ensures (result == n)
{
    ghost unsigned bound = n;
    unsigned i = 0u;
    while (i < n)
        invariant (i <= bound && bound == n)
    {
        ghost unsigned seen = i;
        ++i;
    }
    return i;
}

verified unsigned doubled(unsigned x)
    expects (x < 4u)
    ensures (result == x + x)
{
    ghost unsigned expected = twice(x), spare = expected;
    unsigned y = x;
    ghost bool small = y < 4u;
    y = y + x;
    return y;
}

int main() {
    std::printf("%u %u\n", count(3u), doubled(2u));
}
