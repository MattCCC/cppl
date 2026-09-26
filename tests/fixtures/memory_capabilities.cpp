// Memory capabilities owed at verified calls (SPEC.md 12.10 VERIFIED-013,
// VERIFIED-037, VERIFIED-043).
//
// A callee's contract may state `readable`/`writable` of a pointer parameter.
// Every verified call owes it: the caller passes one of its own pointer
// parameters and its own contract states a capability of the same kind on it.
// Where either side states an element count, the callee's may not exceed the
// caller's, and that comparison is a value obligation the kernel decides. Each
// refused counterpart is written out in `negative/memory_capability_*.cpp`.
#include <cstdio>

verified void touch(unsigned* p)
    expects (writable(p))
{
    *p = 7u;
}

verified void fill(unsigned* p, unsigned n)
    expects (writable(p, n))
{
    if (n > 1u) {
        p[1u] = 5u;
    }
}

// The caller holds exactly what the callee requires.
verified void holds(unsigned* q)
    expects (writable(q))
{
    touch(q);
}

// A sized capability passed on whole: `m <= m`.
verified void holds_sized(unsigned* q, unsigned m)
    expects (writable(q, m))
{
    fill(q, m);
}

// A narrower region, and one object of a sized region, each under the path
// fact that makes the count fit: `m - 1u <= m` and `1u <= m` when `m > 3u`.
verified void narrower(unsigned* q, unsigned m)
    expects (writable(q, m))
{
    if (m > 3u) {
        fill(q, m - 1u);
        touch(q);
    }
}

// SPEC: VERIFIED-038
// A place formed by a read is written and read again under the capabilities
// stated for `p` itself, which a member function names past its implicit
// object. Twins of `memory_capability_write_after_read` and
// `memory_capability_read_after_write`.
struct Cell {
    unsigned seen;

    verified unsigned swap_in(unsigned* p)
        expects (readable(p) && writable(p))
        ensures (result == result)
    {
        unsigned first = *p;
        *p = 5u;
        return *p + first;
    }
};

int main() {
    unsigned cells[4] = {0u, 0u, 0u, 0u};
    Cell cell{0u};
    unsigned spare = 1u;
    if (cell.swap_in(&spare) != 6u) {
        return 1;
    }
    holds(&cells[0]);
    holds_sized(&cells[0], 4u);
    narrower(&cells[0], 4u);
    std::printf("%u %u\n", cells[0], cells[1]);
    return cells[0] == 7u && cells[1] == 5u ? 0 : 1;
}
