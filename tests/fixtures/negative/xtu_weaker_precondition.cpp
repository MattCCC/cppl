// SPEC: TUBOUND-004
// clamp4 declared here, without library.hpp, with a weaker precondition than
// the one library.cpp proved it under. A caller would owe less than the proof
// assumed: clamp4(150) is outside what was proven.
verified unsigned clamp4(unsigned x)
    expects (x < 1000u)
    ensures (result < 4u);

verified unsigned clamped(unsigned y)
    expects (y < 200u)
    ensures (result < 4u)
{
    return clamp4(y);
}
