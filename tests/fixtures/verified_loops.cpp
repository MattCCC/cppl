// Loops verified against explicit invariants: each invariant holds on entry,
// every iteration re-establishes it, and what follows the loop knows only the
// invariants and that the condition failed. Partial correctness only.

verified unsigned count_up(unsigned n)
    ensures(result == n)
{
    unsigned i = 0u;
    while (i < n)
        invariant(i <= n)
    {
        i = i + 1u;
    }
    return i;
}

verified unsigned count_for(unsigned n)
    ensures(result == n)
{
    unsigned last = 0u;
    for (unsigned i = 0u; i < n; ++i)
        invariant(i <= n)
        invariant(last == i)
    {
        last += 1u;
    }
    return last;
}

// Two locals move together; the invariant relates them.
verified unsigned double_count(unsigned n)
    expects(n <= 1000u)
    ensures(result == 2u * n)
{
    unsigned i = 0u;
    unsigned total = 0u;
    while (i != n)
        invariant(total == 2u * i)
    {
        i++;
        total += 2u;
    }
    return total;
}

// A local the loop never writes keeps its value past the loop.
verified unsigned untouched(unsigned n, unsigned k)
    ensures(result == k)
{
    unsigned kept = k;
    unsigned i = 0u;
    while (i < n)
        invariant(i <= n)
    {
        ++i;
    }
    return kept;
}

// Counting down to zero from a precondition.
verified unsigned drain(unsigned n)
    ensures(result == 0u)
{
    unsigned left = n;
    while (left > 0u)
        invariant(left <= n)
    {
        left -= 1u;
    }
    return left;
}

// A return inside the loop, and a break out of it.
verified unsigned find_limit(unsigned n, unsigned limit)
    ensures(result <= limit)
{
    unsigned i = 0u;
    while (i < n)
        invariant(i <= n)
    {
        if (i == limit)
            return i;
        if (i > limit)
            break;
        ++i;
    }
    if (i <= limit)
        return i;
    return limit;
}

// `continue` ends the iteration early; the invariant must still hold there.
verified unsigned skip_some(unsigned n)
    ensures(result <= n)
{
    unsigned seen = 0u;
    for (unsigned i = 0u; i < n; ++i)
        invariant(seen <= i)
        invariant(i <= n)
    {
        if (i == 3u)
            continue;
        ++seen;
    }
    return seen;
}

// Nested loops, each with its own invariant.
verified unsigned grid(unsigned rows, unsigned columns)
    ensures(result == rows * columns)
{
    unsigned total = 0u;
    for (unsigned r = 0u; r < rows; ++r)
        invariant(r <= rows)
        invariant(total == r * columns)
    {
        for (unsigned c = 0u; c < columns; ++c)
            invariant(c <= columns)
            invariant(total == r * columns + c)
        {
            ++total;
        }
    }
    return total;
}

verified unsigned clamp(unsigned x)
    ensures(result <= 10u)
{
    if (x <= 10u)
        return x;
    return 10u;
}

// Verified calls inside a loop: each call's precondition is proven where the
// loop makes it, under the invariant and the condition.
verified unsigned bounded_step(unsigned x)
    expects(x < 10u)
    ensures(result == x + 1u)
{
    return x + 1u;
}

verified unsigned walk_to_ten()
    ensures(result == 10u)
{
    unsigned i = 0u;
    while (i < 10u)
        invariant(i <= 10u)
    {
        i = bounded_step(i);
    }
    return i;
}

// A loop function called from another verified function: the caller's
// contract is partial as well.
verified unsigned count_twice(unsigned n)
    ensures(result == n)
{
    unsigned once = count_up(n);
    return clamp(0u) * 0u + once;
}

// Several preconditions conjoin: the body supposes each of them, and a caller
// proves each of them where it makes the call.
verified unsigned count_from(unsigned start, unsigned n)
    expects(start == 0u)
    expects(n == 3u)
    ensures(result == 3u)
{
    unsigned i = start;
    while (i < n)
        invariant(i <= n)
    {
        i = i + 1u;
    }
    return i;
}

verified unsigned count_three()
    ensures(result == 3u)
{
    return count_from(0u, 3u);
}

int main() {
    if (count_three() != 3u)
        return 6;
    if (count_up(5u) != 5u || count_for(7u) != 7u || double_count(4u) != 8u)
        return 1;
    if (untouched(3u, 9u) != 9u || drain(6u) != 0u)
        return 2;
    if (find_limit(10u, 4u) != 4u || find_limit(3u, 8u) != 3u)
        return 3;
    if (skip_some(6u) != 5u || walk_to_ten() != 10u || count_twice(4u) != 4u)
        return 4;
    if (grid(3u, 4u) != 12u || grid(0u, 9u) != 0u)
        return 5;
    return 0;
}
