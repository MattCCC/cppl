// SPEC: TUBOUND-003
// `clamped` in cross_tu/client.cpp, claiming more than clamp4's recorded
// contract gives: clamp4 returns 3 for 42, so `result < 3` is false, and
// nothing but the imported postcondition speaks about clamp4's result here.
#include "library.hpp"

verified unsigned clamped(unsigned y)
    expects (y < 50u)
    ensures (result < 3u)
{
    return clamp4(y);
}
