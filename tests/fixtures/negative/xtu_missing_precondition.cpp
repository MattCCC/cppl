// SPEC: TUBOUND-003
// `clamped` in cross_tu/client.cpp, whose precondition no longer establishes
// clamp4's: y may be 150, and clamp4 is proven only for x < 100.
#include "library.hpp"

verified unsigned clamped(unsigned y)
    expects (y < 200u)
    ensures (result < 4u)
{
    return clamp4(y);
}
