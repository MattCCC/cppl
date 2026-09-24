// Termination (SPEC.md 22, 23, 24.3): `decreases` on loops and functions.
//
// A measure is an unsigned value, or a lexicographic list of them, that every
// continuing iteration and every recursive call makes strictly smaller. Where
// every loop a function runs has one, and every function it calls terminates,
// its contract is total; otherwise it is partial, and the report says so.
// Recursion is verified only with a measure. `tests/e2e/termination.sh` checks
// the counts and the program; each refused counterpart is written out in
// `negative/termination_*.cpp`.
#include <cstdio>

// --- Recursion ------------------------------------------------------------------

verified unsigned count_down(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return count_down(n - 1u);
}

// Mutual recursion: each call within the group descends the shared measure.
unsigned odd_steps(unsigned n);

verified unsigned even_steps(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return odd_steps(n - 1u);
}

verified unsigned odd_steps(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return even_steps(n - 1u);
}

// A lexicographic measure: the inner call keeps `m` and lowers `n`, the outer
// lowers `m` whatever the inner returned.
verified unsigned ackermann(unsigned m, unsigned n)
    ensures (result >= 0u)
    decreases (m, n)
{
    if (m == 0u) {
        return n + 1u;
    }
    if (n == 0u) {
        return ackermann(m - 1u, 1u);
    }
    return ackermann(m - 1u, ackermann(m, n - 1u));
}

// A precondition is owed at every recursive call, as at any call.
verified unsigned halve_to_zero(unsigned n)
    expects (n < 64u)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return halve_to_zero(n - 1u);
}

// Calling a total function keeps a contract total.
verified unsigned calls_recursion(unsigned n)
    ensures (result == 0u)
{
    return count_down(n);
}

// --- Loops ----------------------------------------------------------------------

// A lexicographic loop measure: the column falls, or the row does while the
// column is reset.
verified unsigned grid(unsigned rows, unsigned cols)
    ensures (result == 0u)
{
    unsigned r = rows;
    unsigned c = cols;
    while (r > 0u)
        invariant (c <= cols)
        decreases (r, c)
    {
        if (c > 0u) {
            c = c - 1u;
        } else {
            r = r - 1u;
            c = cols;
        }
    }
    return r;
}

// A `do` loop: the invariant is owed before the body first runs, and the
// measure falls on every path back to the body.
verified unsigned drain(unsigned n)
    expects (n > 0u)
    ensures (result == 0u)
{
    unsigned i = n;
    do
        invariant (i > 0u)
        decreases (i)
    {
        i = i - 1u;
    } while (i > 0u);
    return i;
}

// A `for` without a condition, left by a `break`; a `continue` also falls.
verified unsigned search(unsigned n)
    ensures (result == n)
{
    unsigned i = 0u;
    for (;;)
        invariant (i <= n)
        decreases (n - i)
    {
        if (i == n) {
            break;
        }
        if (i == 7u) {
            ++i;
            continue;
        }
        ++i;
    }
    return i;
}

// Nested loops, each with its own measure.
verified unsigned nested(unsigned n)
    ensures (result == n)
{
    unsigned outer = 0u;
    while (outer < n)
        invariant (outer <= n)
        decreases (n - outer)
    {
        unsigned inner = 0u;
        while (inner < outer)
            invariant (inner <= outer)
            decreases (outer - inner)
        {
            ++inner;
        }
        ++outer;
    }
    return outer;
}

// A measure may read ghost state: the bound is recorded for the proof.
verified unsigned ghost_bound(unsigned n)
    ensures (result == n)
{
    ghost unsigned bound = n;
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n && bound == n)
        decreases (bound - i)
    {
        ++i;
    }
    return i;
}

// A function that asks to terminate without recursing: its loop has a measure.
verified unsigned requested(unsigned n)
    ensures (result == n)
    decreases (n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
        decreases (n - i)
    {
        ++i;
    }
    return i;
}

// No measure: proven, and partial correctness only.
verified unsigned partial(unsigned n)
    ensures (result == n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
    {
        ++i;
    }
    return i;
}

int main() {
    std::printf("%u %u %u %u %u %u %u %u %u %u %u %u\n", count_down(5u), even_steps(4u), ackermann(2u, 2u),
                halve_to_zero(9u), calls_recursion(3u), grid(2u, 3u), drain(4u), search(9u), nested(4u),
                ghost_bound(3u), requested(6u), partial(2u));
}
