// SPEC: TUBOUND-006, TUBOUND-009
// `through_middle` in cross_tu/client.cpp. middle.cpp proved doubled through
// library.cpp's contract of clamp4, so doubled's record is usable only where
// that contract is imported too, as the very record doubled was proven
// against. Compiled importing middle's interface alone, it is refused; with
// both, it verifies.
#include "middle.hpp"

verified unsigned through_middle(unsigned y)
    expects (y < 50u)
    ensures (result < 7u)
{
    return doubled(y);
}
