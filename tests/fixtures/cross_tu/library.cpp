// Proves the contracts library.hpp declares (SPEC.md TU-002, TU-003).
//
// A definition with no clauses of its own takes the contract of the verified
// declaration it defines. One whose body states loop clauses is itself marked
// verified, and restates the declaration's contract, which must mean the same.
//
// Compiled with `--cppl-emit-interface`, this unit records each contract it
// proved, with what that proof rests on (SPEC.md TUBOUND-002, TUBOUND-006).
#include "library.hpp"

pure unsigned zero() {
    return 0u;
}

// False, and trusted: whatever rests on it is PROVEN only relative to it, in
// this unit and in every unit that uses a contract proven through it.
trusted law broken_counter()
    proves (zero() == 1u);

proof counter_is_one()
    proves (zero() == 1u)
{
    exact broken_counter;
}

unsafe unsigned read_device();

// Internal linkage: a different function in every unit that could declare it,
// so it is verified here and recorded in no interface (SPEC.md TUBOUND-004).
namespace {
verified unsigned below_four(unsigned x)
    ensures (result < 4u)
{
    if (x < 4u) {
        return x;
    }
    return 3u;
}
} // namespace

unsigned clamp4(unsigned x) {
    return below_four(x);
}

Small small_of(unsigned x) {
    if (x < 4u) {
        return x;
    }
    return 0u;
}

verified unsigned count_to(unsigned n)
    ensures (result == n)
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

verified unsigned count_up(unsigned n)
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

verified unsigned never_seven(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    if (x == 7u) {
        contradiction counter_is_one;
    }
    return x;
}

verified unsigned sensor()
    ensures (result <= 100u)
{
    unsigned x = 0u;
    unsafe {
        x = read_device();
    }
    if (x > 100u) {
        return 100u;
    }
    return x;
}

void bump(unsigned& counter) {
    counter = counter + 1u;
}

unsigned step(unsigned x) {
    return x;
}

unsigned long step(unsigned long x) {
    return x + 1ul;
}

template <> unsigned bound<4u>(unsigned x) {
    if (x < 4u) {
        return x;
    }
    return 0u;
}

unsafe unsigned read_device() {
    return 42u;
}
