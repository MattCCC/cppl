// SPEC: TUBOUND-003, TUBOUND-001, TU-004
// A verified declaration no unit proved. Its contract is well formed and true
// of the body a caller might imagine, but a declaration is not evidence, and
// no imported interface records it.
#include "library.hpp"

verified unsigned identity(unsigned x)
    ensures (result == x);

verified unsigned same(unsigned x)
    ensures (result == x)
{
    return identity(x);
}
