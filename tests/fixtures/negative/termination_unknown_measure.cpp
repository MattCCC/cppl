// SPEC: TERMINATION-005
// A call at a value nothing bounds: `pick`'s contract says nothing of how its
// result compares with `n`, so no descent follows, however likely one is.
verified unsigned pick(unsigned n)
    ensures (result >= 0u)
{
    return n;
}

verified unsigned chase(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return chase(pick(n - 1u));
}

int main() {
    return static_cast<int>(chase(3u));
}
