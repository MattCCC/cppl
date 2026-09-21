// Machine arithmetic: unsigned +, -, * are the ring of integers modulo 2^32,
// comparisons are decided by the machine type, and order consequences are
// proven by linear arithmetic that accounts for wrapping.

verified unsigned reassociated(unsigned x, unsigned y, unsigned z)
    ensures (result == (x + y) + z)
{
    return x + (y + z);
}

verified unsigned distributed(unsigned x, unsigned y, unsigned z)
    ensures (result == x * y + x * z)
{
    return x * (y + z);
}

verified unsigned square_of_successor(unsigned x)
    ensures (result == x * x + 2u * x + 1u)
{
    return (x + 1u) * (x + 1u);
}

verified unsigned cancelled(unsigned x, unsigned y)
    ensures (result == x)
{
    return (x + y) - y;
}

verified unsigned plus_two(unsigned x)
    ensures (result == x + 2u)
{
    unsigned y = x + 1u;
    return y + 1u;
}

verified unsigned wraps_to_zero()
    ensures (result == 0u)
{
    return 4294967295u + 1u;
}

verified unsigned countdown(unsigned n)
    expects (n >= 3u)
    ensures (result == n - 3u)
{
    unsigned m = n;
    m = m - 1u;
    m = m - 1u;
    m = m - 1u;
    return m;
}

// Order consequences of guards.
verified unsigned weakened(unsigned x)
    ensures (result <= 10u)
{
    if (x < 10u)
        return x;
    return 10u;
}

verified unsigned transitive(unsigned x)
    ensures (result < 20u)
{
    if (x < 5u)
        return x;
    return 0u;
}

// A difference is at most its minuend once the subtrahend is known not to
// exceed it; otherwise it would wrap.
verified unsigned bounded_difference(unsigned x, unsigned y)
    ensures (result <= x)
{
    if (y <= x)
        return x - y;
    return x;
}

// A successor is larger once the value is known not to be the maximum.
verified unsigned successor_below(unsigned i, unsigned n)
    expects (i < n)
    ensures (result <= n)
{
    return i + 1u;
}

// The inner path is unreachable: its guards contradict each other, and the
// contradiction is what proves it.
verified unsigned unreachable_path(unsigned x)
    ensures (result <= 10u)
{
    if (x <= 10u) {
        if (x > 10u)
            return 11u;
        return x;
    }
    return 10u;
}

verified int signed_transitive(int x)
    ensures (result < 20)
{
    if (x <= 10)
        return x;
    return 0;
}

// Contracts compose through arithmetic: each call's precondition follows from
// the caller's precondition and the earlier call's postcondition.
verified unsigned predecessor(unsigned x)
    expects (x > 0u)
    ensures (result == x - 1u)
{
    return x - 1u;
}

verified unsigned minus_two(unsigned x)
    expects (x >= 2u)
    ensures (result == x - 2u)
{
    return predecessor(predecessor(x));
}

law subtraction_undoes_addition(unsigned x, unsigned y)
    proves ((x - y) + y == x);

law successor_stays_below(unsigned i, unsigned n)
    expects (i < n)
    proves (i + 1u <= n);

law product_commutes(unsigned x, unsigned y)
    proves (x * y == y * x);

proof product_commutes_holds(unsigned x, unsigned y)
    proves (product_commutes(x, y))
{
    refl;
}

int main() {
    if (reassociated(1u, 2u, 3u) != 6u || distributed(2u, 3u, 4u) != 14u)
        return 1;
    if (square_of_successor(3u) != 16u || cancelled(5u, 4294967295u) != 5u)
        return 2;
    if (plus_two(4294967295u) != 1u || wraps_to_zero() != 0u || countdown(10u) != 7u)
        return 3;
    if (weakened(3u) != 3u || weakened(30u) != 10u || transitive(4u) != 4u)
        return 4;
    if (bounded_difference(5u, 3u) != 2u || bounded_difference(3u, 5u) != 3u)
        return 5;
    if (successor_below(3u, 4u) != 4u || unreachable_path(4u) != 4u)
        return 6;
    if (signed_transitive(-7) != -7 || signed_transitive(15) != 0)
        return 7;
    if (minus_two(2u) != 0u || minus_two(9u) != 7u)
        return 8;
    return 0;
}
