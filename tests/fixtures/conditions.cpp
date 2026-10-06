// SPEC: EXPR-016, SPECEXPR-002, LOOP-004
// Conditions in verified bodies, from the shapes realistic programs write:
// loop invariants stating `&&` and `||`, nested in each other, each operand
// specified on its own. e2e/conditions.sh verifies and runs this;
// negative/conditions.sh refuses its false twins.
#include <cstdio>

// A flag the loop may set: the invariant states which of two situations
// holds, each a conjunction.
verified unsigned countdown(unsigned n, unsigned mark)
    ensures (result <= 1u)
{
    unsigned i = n;
    unsigned seen = 0u;
    while (i > 0u)
        invariant ((seen == 1u && i < n) || (seen == 0u && i <= n))
        decreases (i)
    {
        if (i == mark) {
            seen = 1u;
        }
        i = i - 1u;
    }
    return seen;
}

// What the loop remembers is either nothing yet or a position below the limit.
verified unsigned last_below(unsigned n, unsigned limit)
    ensures (result == n || result < limit)
{
    unsigned last = n;
    for (unsigned i = 0u; i < n; ++i)
        invariant (i <= n && (last == n || last < limit))
        decreases (n - i)
    {
        if (i < limit) {
            last = i;
        }
    }
    return last;
}

int main() {
    std::printf("%u %u %u %u\n", countdown(5u, 2u), countdown(5u, 9u), last_below(6u, 3u), last_below(4u, 0u));
    return 0;
}
