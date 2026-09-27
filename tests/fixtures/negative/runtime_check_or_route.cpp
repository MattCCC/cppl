// SPEC: RUNTIMECHECK-002, INTERACT-022
// The route where `a || b` holds establishes neither side on its own, so the
// second disjunct lets a negative value through. Twin of `positive_or_one`.
type Positive = int where (self > 0);

verified int either(int raw)
    ensures (result > 0)
{
    if (raw > 0 || raw < -5) {
        Positive p = raw;
        return p;
    }
    return 1;
}

int main() {
    return either(-10);
}
