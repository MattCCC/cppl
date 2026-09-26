// SPEC: TUBOUND-004, TERMINATION-006
// `cross_tu/identity_client.cpp` declaring `counted_down` without asking it to
// terminate. Which measure proved termination is the proof's affair and does
// not have to be repeated, but whether termination is part of the claim is the
// contract's: a declaration without `decreases` states another contract from
// the one `identity.cpp` recorded, and is refused.

verified unsigned counted_down(unsigned n)
    ensures (result == n);

verified unsigned uses_counted(unsigned n)
    ensures (result == n)
{
    return counted_down(n);
}
