// SPEC: TUBOUND-004
// clamp4 declared here, without library.hpp, with a stronger postcondition
// than the one library.cpp proved. The contract this unit states is the one it
// would rely on, so it must be the one recorded; `result < 2` is false of
// clamp4(3).
verified unsigned clamp4(unsigned x)
    expects (x < 100u)
    ensures (result < 2u);

verified unsigned clamped(unsigned y)
    expects (y < 50u)
    ensures (result < 2u)
{
    return clamp4(y);
}
