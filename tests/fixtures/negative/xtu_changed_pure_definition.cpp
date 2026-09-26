// SPEC: TUBOUND-004
// `bounded` is declared as ranked.hpp declares it, but `limit` reaches a `base`
// that computes something else here: the contract means another thing, so the
// record of ranked.cpp is not this unit's contract.
pure unsigned base() {
    return 2u;
}

pure unsigned limit() {
    return base() + 3u;
}

verified unsigned bounded(unsigned x)
    expects (x < limit())
    ensures (result < limit());

verified unsigned use(unsigned x)
    expects (x < 4u)
    ensures (result < 5u)
{
    return bounded(x);
}
