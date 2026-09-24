// Ghost state: proof-only locals of a verified body (SPEC.md 25, GRAMMAR.md 21).
//
// A ghost local records a value for the proof. Runtime values may be copied
// into it, other ghosts and pure functions may compute it, and loop clauses and
// claims may read it; nothing that runs may. The whole declaration leaves the
// program. `tests/e2e/ghost_state.sh` checks that this verifies, runs, and
// leaves no ghost in the program; each refused counterpart is written out in
// `negative/ghost_*.cpp`.
#include <cstdio>

pure unsigned twice(unsigned x) {
    return x + x;
}

type Small = unsigned where (self < 10u);

proof same(unsigned v)
    proves (v == v)
{
    refl;
}

// A snapshot of a parameter, read by a loop invariant.
verified unsigned count(unsigned n)
    ensures (result == n)
{
    ghost unsigned bound = n;
    unsigned i = 0u;
    while (i < n)
        invariant (i <= bound && bound == n)
    {
        ++i;
    }
    return i;
}

// A runtime value copied before and after it changes; a ghost computed from
// another ghost; one read by a claim's evidence. The branch is on runtime
// state: a branch on a ghost would be refused.
verified unsigned snapshot(unsigned x)
    expects (x < 5u)
    ensures (result < 5u)
{
    unsigned y = x;
    ghost unsigned before = y;
    y = 0u;
    ghost unsigned after = before + y;
    if (y != 0u) {
        contradiction same(after);
    }
    return x;
}

// A pure function, a Boolean, a refinement it is proven to satisfy, and two
// ghosts in one declaration.
verified unsigned doubled(unsigned x)
    expects (x < 4u)
    ensures (result == x + x)
{
    ghost unsigned expected = twice(x), spare = expected;
    ghost bool small = x < 4u;
    ghost Small digit = x + 1u;
    return x + x;
}

// A ghost declared in a loop body is one per iteration.
verified unsigned sum_to(unsigned n)
    expects (n < 100u)
    ensures (result == n)
{
    unsigned total = 0u;
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n && total == i)
    {
        ghost unsigned previous = total;
        total = total + 1u;
        ++i;
    }
    return total;
}

int main() {
    std::printf("%u %u %u %u\n", count(4u), snapshot(3u), doubled(3u), sum_to(5u));
}
