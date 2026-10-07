// SPEC: REFINE-010, RUNTIMECHECK-011
// `accepted_units` of v1_sweep/intake.cpp without its validation: a request's
// units enter a Quantity as they arrived from outside the program, which may
// be any `unsigned`, so the crossing is refused rather than assumed.
#include "intake.hpp"

verified unsigned accepted_units(unsigned requested)
    ensures (result <= 1000u && (result == 0u || result == requested))
{
    const Quantity units = requested;
    return units;
}
