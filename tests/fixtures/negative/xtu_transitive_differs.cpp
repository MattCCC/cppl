// SPEC: TUBOUND-004
// `cross_tu/identity_client.cpp` with the pure function the postcondition's
// function calls defined otherwise: `inner_step` adds 2 here and 1 in the unit
// that proved `stepped`. The postcondition's own function is spelled the same,
// so only an identity that follows every pure definition the contract reaches,
// transitively, tells the two contracts apart; the one recorded is not the one
// declared here, and the claim below, false of the real `stepped`, is refused
// with it.

inline pure unsigned inner_step(unsigned x) {
    return x + 2u;
}

inline pure unsigned outer_step(unsigned x) {
    return inner_step(x) + 1u;
}

verified unsigned stepped(unsigned x)
    expects (x < 100u)
    ensures (result == outer_step(x));

verified unsigned uses_stepped(unsigned x)
    expects (x < 50u)
    ensures (result == x + 3u)
{
    return stepped(x);
}
