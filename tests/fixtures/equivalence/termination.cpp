// Termination measures (SPEC.md 22, 24.3, 24.4, Annex M).
//
// Erased, this unit must compile to exactly the code `termination.reference.cpp`
// compiles to. A function's `decreases` leaves with its other clauses, and a
// loop's with its invariants; every loop form, `do` and a `for` without a
// condition included, and every recursive call stay as written, and no counter
// or check is added.
#include <cstdio>

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
        ++i;
    }
    return i;
}

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

int main() {
    std::printf("%u %u %u %u %u\n", even_steps(5u), ackermann(2u, 3u), drain(3u), search(6u), grid(2u, 2u));
}
