// SPEC: TU-003
// count_to in cross_tu/library.cpp, whose definition restates its contract as
// `result <= n` rather than the header's `result == n`. One function has one
// contract, so the conflict is refused rather than either statement used.
#include "library.hpp"

verified unsigned count_to(unsigned n)
    ensures (result <= n)
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
