// Contracts, loop clauses and impossible-path claims on runtime code
// (SPEC.md ERASE-002, ERASE-003, ERASE-005, ERASE-016, Annex M).
//
// Erased, this unit must compile to exactly the code `contracts.reference.cpp`
// compiles to. The specifiers `verified` and `pure` leave; every function body,
// every loop, every branch and every other specifier stays; each clause leaves
// no trace; and a claim that a path cannot occur leaves only its `;`.
#include <cstdio>

pure unsigned zero() {
    return 0u;
}

proof nothing()
    proves (zero() == 0u)
{
    refl;
}

verified unsigned successor(unsigned x)
    expects (x < 10u)
    ensures (result == x + 1u)
{
    return x + 1u;
}

verified pure unsigned twice(unsigned x)
    expects (x < 1000u)
    ensures (result == x + x)
{
    return x + x;
}

// Linkage, inlining and exception specifications are ordinary C++ and stay.
static verified unsigned kept_static(unsigned x)
    ensures (result == x)
{
    return x;
}

inline verified unsigned kept_inline(unsigned x)
    ensures (result == x)
{
    return x;
}

verified unsigned kept_noexcept(unsigned x) noexcept
    ensures (result == x)
{
    return x;
}

// Every specialization keeps its code; the contract is checked per
// specialization and leaves none behind.
template <unsigned N>
verified unsigned clamp_to(unsigned x)
    expects (x < N)
    ensures (result < N)
{
    return x;
}

template <unsigned N> unsigned pick(unsigned x) {
    return x;
}

template <>
verified unsigned pick<4u>(unsigned x)
    expects (x < 4u)
    ensures (result < 4u)
{
    return x;
}

verified unsigned count_up(unsigned n)
    ensures (result == n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
    {
        i = i + 1u;
    }
    return i;
}

verified unsigned count_for(unsigned n)
    ensures (result == n)
{
    unsigned last = 0u;
    for (unsigned i = 0u; i < n; ++i)
        invariant ((i <= n) && (last == i))
    {
        last += 1u;
    }
    return last;
}

verified unsigned counted_down(unsigned n)
    ensures (result == 0u)
{
    unsigned left = n;
    while (left > 0u)
        invariant (left <= n)
        decreases (left)
    {
        left = left - 1u;
    }
    return left;
}

// A claim as the whole body of an unbraced `if`: its `;` stays, so the return
// below it stays outside the `if`.
verified unsigned unbraced(unsigned x)
    expects (x < 5u)
    ensures (result == x)
{
    if (x >= 5u)
        contradiction nothing;
    return x;
}

// A claim with code after it on its path: that code stays, unreached.
verified unsigned after(unsigned x)
    expects (x < 5u)
    ensures (result < 5u)
{
    if (x >= 5u) {
        contradiction nothing;
        return 7u;
    }
    return x;
}

verified unsigned uses_call(unsigned x)
    expects (x < 5u)
    ensures (result == x + 1u)
{
    unsigned y = successor(x);
    if (y == 0u) {
        contradiction nothing;
    }
    return y;
}

verified void set(int& x)
    ensures (x == 1)
{
    x = 1;
}

int main() {
    int written = 0;
    set(written);
    std::printf("%u %u %u %u %u %u %u %u %u %u %u %u %u %d %d\n", successor(3u), twice(4u), kept_static(5u),
                kept_inline(6u), kept_noexcept(7u), clamp_to<4u>(3u) + clamp_to<8u>(7u), pick<4u>(2u) + pick<9u>(8u),
                count_up(5u), count_for(6u), counted_down(4u), unbraced(3u), after(2u), uses_call(3u), written,
                static_cast<int>(noexcept(kept_noexcept(0u))));
}
