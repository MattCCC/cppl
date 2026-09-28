// Refused twin of fixtures/runtime_validation_cross_tu/validate.cpp (tests/negative/refused_twins.sh): the same
// program, except that positive_or_one lets 0 reach its crossing into Positive.
// A contract whose proof rests on a runtime check, recorded in this unit's
// verification interface with the site it rests on (SPEC.md RUNTIMECHECK-015).
#include "validate.hpp"

verified int positive_or_one(int raw)
    ensures (result > 0)
{
    if (raw < 0) {
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
