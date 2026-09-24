// SPEC: TERMINATION-007, TERMINATION-001
// Recursion is verified only with a measure every recursive call descends.
// Without one, the contract would be supposed at the recursive call before it
// is proven, which proves anything: this one claims a result it never computes.
verified unsigned forever(unsigned n)
    ensures (result == 7u)
{
    return forever(n);
}

int main() {
    return static_cast<int>(forever(1u));
}
