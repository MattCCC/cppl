// SPEC: UNSAFE-005
// TRUST.md TCB-UNSAFE-002: storage a reference parameter designates is reachable
// from code outside this body, so an unsafe block may change it without naming
// it. What the precondition said of `r` is not known after the block.
unsafe void poke() {
}

verified unsigned through_reference(unsigned& r)
    expects (r < 10u)
    ensures (result < 10u)
{
    unsafe {
        poke();
    }
    return r;
}

int main() {
    unsigned v = 1u;
    return static_cast<int>(through_reference(v));
}
