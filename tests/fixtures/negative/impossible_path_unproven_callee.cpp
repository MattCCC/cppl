// A claim may rest on a verified callee's postcondition only once that
// postcondition is proven (SPEC.md VERIFIED-014, VERIFIED-023). `successor`
// does not satisfy its contract, so the fact `y == x + 1` that would rule out
// `y == 0` is not available, and the claim that relies on it is not shown to be
// unreachable.
pure unsigned zero() {
    return 0u;
}

proof nothing() proves (zero() == 0u)
{
    refl;
}

verified unsigned successor(unsigned x)
    expects (x < 10u)
    ensures (result == x + 1u)
{
    return x;
}

verified unsigned uses_call(unsigned x)
    expects (x < 5u)
    ensures (result == 0u)
{
    unsigned y = successor(x);
    if (y == 0u) {
        contradiction nothing;
    }
    return 0u;
}

int main() {
    return 0;
}
