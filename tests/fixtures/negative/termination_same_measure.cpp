// SPEC: TERMINATION-005, TERMINATION-006
// A recursive call at the same measure does not descend, so the induction
// hypothesis it supposes is not justified, and the false postcondition it
// would carry is not proven.
verified unsigned forever(unsigned n)
    ensures (result == 7u)
    decreases (n)
{
    return forever(n);
}

int main() {
    return static_cast<int>(forever(1u));
}
