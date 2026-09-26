// SPEC: TUBOUND-003, TUBOUND-004
// `small` in cross_tu/client.cpp, claiming more than the refinement small_of's
// result carries across the boundary: `Small` is `self < 4u`, and small_of(3)
// is 3.
#include "library.hpp"

verified unsigned small(unsigned y)
    expects (y < 50u)
    ensures (result < 3u)
{
    return small_of(y);
}
