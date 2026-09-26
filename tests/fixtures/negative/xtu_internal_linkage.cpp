// SPEC: TUBOUND-004
// A verified function with internal linkage, declared and never defined. It
// is a different function in every unit, however its identity is spelled, so
// no other unit's proof can describe it, and no interface is consulted for it.
static verified unsigned hidden(unsigned x)
    ensures (result == x);

verified unsigned same(unsigned x)
    ensures (result == x)
{
    return hidden(x);
}
