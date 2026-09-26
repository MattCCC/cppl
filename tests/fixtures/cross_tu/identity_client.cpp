// SPEC: TUBOUND-004, TUBOUND-007
// A consumer of `identity.cpp` that declares its contracts itself rather than
// including `identity.hpp`: the same pure functions, by content, down to the
// one the postcondition's function calls, and `counted_down` asking to
// terminate under a measure of its own. Both match the records. The refused
// twins change one thing each: `negative/xtu_transitive_differs.cpp` the inner
// pure function, `negative/xtu_measure_unrequested.cpp` the request to
// terminate.

inline pure unsigned inner_step(unsigned x) {
    return x + 1u;
}

inline pure unsigned outer_step(unsigned x) {
    return inner_step(x) + 1u;
}

verified unsigned stepped(unsigned x)
    expects (x < 100u)
    ensures (result == outer_step(x));

verified unsigned counted_down(unsigned n)
    ensures (result == n)
    decreases (n);

verified unsigned uses_stepped(unsigned x)
    expects (x < 50u)
    ensures (result == x + 2u)
{
    return stepped(x);
}

verified unsigned uses_counted(unsigned n)
    ensures (result == n)
    decreases (n)
{
    return counted_down(n);
}
