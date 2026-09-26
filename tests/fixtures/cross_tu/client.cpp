// Uses the contracts library.cpp proved, through its verification interface
// (SPEC.md TUBOUND-003 to TUBOUND-007). No body of library.cpp is visible here.
//
// Every goal below is one this unit can close only with the callee's recorded
// contract: the callee's body is not here, so nothing else says what it
// returns. Each refused counterpart is written out in
// `negative/xtu_*.cpp`, differing from the function it mirrors in one thing.
#include "library.hpp"
#include "middle.hpp"

#include <cstdio>

// The caller proves clamp4's precondition, and learns only its postcondition.
verified unsigned clamped(unsigned y)
    expects (y < 50u)
    ensures (result < 4u)
{
    return clamp4(y);
}

// A refined result carries its predicate across the boundary.
verified unsigned small(unsigned y)
    expects (y < 50u)
    ensures (result < 4u)
{
    return small_of(y);
}

// Total, because count_to is total and this body has no loop.
verified unsigned counted(unsigned n)
    ensures (result == n)
    decreases (n)
{
    return count_to(n);
}

// Partial, because count_up is: it holds only if count_up returns.
verified unsigned counted_up(unsigned n)
    ensures (result == n)
{
    return count_up(n);
}

// Rests on library.cpp's trusted law, through never_seven's contract.
verified unsigned not_seven(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    return never_seven(x);
}

// Rests on library.cpp's unsafe block, through sensor's contract.
verified unsigned reading()
    ensures (result <= 100u)
{
    return sensor();
}

// The storage bump writes takes the post-state its contract states.
verified unsigned bumped(unsigned start)
    expects (start < 50u)
    ensures (result < 51u)
{
    unsigned counter = start;
    bump(counter);
    return counter;
}

// Each overload's own contract, selected by Clang.
verified unsigned long stepped(unsigned narrow, unsigned long wide)
    ensures (result == wide + 1ul)
{
    unsigned kept = step(narrow);
    return step(wide);
}

// The specialization library.cpp proved.
verified unsigned bounded(unsigned x)
    ensures (result < 4u)
{
    return bound<4u>(x);
}

// Through a third unit: middle.cpp proved doubled through clamp4.
verified unsigned through_middle(unsigned y)
    expects (y < 50u)
    ensures (result < 7u)
{
    return doubled(y);
}

// Rests on clamp4's recorded contract through `clamped`, a function of this
// unit, and on what that record rests on.
verified unsigned via_local(unsigned y)
    expects (y < 50u)
    ensures (result < 4u)
{
    return clamped(y);
}

// Proven outright: it calls nothing from another unit.
verified unsigned own(unsigned y)
    ensures (result == y)
{
    return y;
}

int main() {
    unsigned counter = 3u;
    bump(counter);
    std::printf("%u %u %u %u %u %u %u %lu %u %u %u %u\n", clamped(42u), small(2u), counted(5u), counted_up(6u),
                not_seven(3u), reading(), bumped(9u), stepped(1u, 9ul), bounded(7u), through_middle(30u), counter,
                via_local(1u));
    return 0;
}
