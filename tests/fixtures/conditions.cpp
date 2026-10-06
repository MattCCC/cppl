// SPEC: EXPR-016, SPECEXPR-002, LOOP-004
// Conditions in verified bodies, from the shapes realistic programs write:
// loop invariants stating `&&` and `||`, nested in each other, each operand
// specified on its own; a ghost snapshot related to the loop case by case;
// callers taking apart the disjunction inside a callee's postcondition; and a
// flag that decides a branch. e2e/conditions.sh verifies and runs this;
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

// The snapshot relates the loop to its entry case by case: `b0 > 0u` holds or
// fails, and where it fails the loop never ran.
verified unsigned gcd(unsigned a, unsigned b)
    ensures (b == 0u -> result == a)
{
    ghost unsigned a0 = a;
    ghost unsigned b0 = b;
    while (b != 0u)
        invariant (b0 > 0u || (a == a0 && b == 0u))
        decreases (b)
    {
        const unsigned t = a % b;
        a = b;
        b = t;
    }
    return a;
}

// Each callee states which argument it returns as a disjunction inside a
// conjunction; a caller takes it apart.
template <typename T>
verified T min_of(T a, T b)
    ensures (result <= a && result <= b && (result == a || result == b))
{
    return b < a ? b : a;
}

template <typename T>
verified T max_of(T a, T b)
    ensures (result >= a && result >= b && (result == a || result == b))
{
    return a < b ? b : a;
}

verified unsigned clamp(unsigned v, unsigned lo, unsigned hi)
    expects (lo <= hi)
    ensures (lo <= result && result <= hi && (result == v || result == lo || result == hi))
{
    return min_of(max_of(v, lo), hi);
}

verified int median(int a, int b, int c)
    ensures (result == a || result == b || result == c)
{
    return max_of(min_of(a, b), min_of(max_of(a, b), c));
}

// What a flag computed by a selection says is known case by case where it
// decides a branch, and a disjunction no side of which holds alone holds by
// one in each case.
verified unsigned either(unsigned x, unsigned y)
    ensures (result <= 1u && (result == 0u || x > 0u || y > 0u) && (result == 1u || (x == 0u && y == 0u)))
{
    const bool any = x > 0u ? true : y > 0u;
    if (any) {
        return 1u;
    }
    return 0u;
}

verified unsigned bump(unsigned count, unsigned x)
    expects (count < 100u)
    ensures (result == count || (x > 0u && result == count + 1u))
{
    return count + (x > 0u ? 1u : 0u);
}

int main() {
    std::printf("%u %u %u %u\n", countdown(5u, 2u), countdown(5u, 9u), last_below(6u, 3u), last_below(4u, 0u));
    std::printf("%u %u %u %u %d\n", gcd(12u, 18u), gcd(5u, 0u), clamp(9u, 2u, 4u), clamp(3u, 2u, 4u), median(3, 1, 2));
    std::printf("%u %u %u %u\n", either(0u, 4u), either(0u, 0u), bump(7u, 2u), bump(7u, 0u));
    return 0;
}
