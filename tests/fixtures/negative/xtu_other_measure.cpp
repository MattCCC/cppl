// SPEC: TUBOUND-004
// Accepted: this unit asks that `count_down` terminate, as ranked.cpp proved,
// and states another measure. How the proving unit ranked its recursion is
// that unit's proof; across units the contract says only that it terminates,
// and recursion is never verified across units (TUBOUND-008), so no measure of
// this unit is ever compared with it.
pure unsigned base() {
    return 1u;
}

pure unsigned limit() {
    return base() + 3u;
}

verified unsigned count_down(unsigned n)
    ensures (result == 0u)
    decreases (n, 0u);

verified unsigned counted(unsigned n)
    ensures (result == 0u)
{
    return count_down(n);
}
