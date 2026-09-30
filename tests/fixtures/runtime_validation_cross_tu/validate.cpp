// A contract whose proof rests on a validation expression, recorded in this
// unit's verification interface with the site it rests on (SPEC.md
// RUNTIMECHECK-015).
#include "validate.hpp"

verified int positive_or_one(int raw)
    ensures (result > 0)
{
    if (!validate<Positive>(raw)) {
        return 1;
    }
    Positive checked = raw;
    return checked;
}

verified int always_two(int raw)
    ensures (result == 2)
{
    if (raw > 0) {
        return 2;
    }
    return 2;
}
