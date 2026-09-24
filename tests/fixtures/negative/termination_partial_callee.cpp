// SPEC: CORRECT-004, CORRECT-005
// A call contributes total correctness only when the callee's contract is
// total. `spin` has a loop without a measure, so `uses` cannot terminate by
// what is known of it.
verified unsigned spin(unsigned n)
    ensures (result == n)
{
    unsigned i = 0u;
    while (i != n)
        invariant (true)
    {
        i = i + 1u;
    }
    return i;
}

verified unsigned uses(unsigned n)
    ensures (result == n)
    decreases (n)
{
    return spin(n);
}

int main() {
    return static_cast<int>(uses(4u));
}
