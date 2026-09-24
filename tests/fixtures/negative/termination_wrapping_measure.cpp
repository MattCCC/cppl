// SPEC: TERMINATION-005, 22.5
// The order is the machine's own, which does not wrap: at n == 0, n - 1 is the
// largest value, not a smaller one. Without the guard that rules that out, the
// descent is not proven, and the missing base case is not supposed away.
verified unsigned down(unsigned n)
    ensures (result == 1u)
    decreases (n)
{
    return down(n - 1u);
}

int main() {
    return static_cast<int>(down(1u));
}
