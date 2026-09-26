// SPEC: ARITH-006, DEFINEDBEHAVIOR-001
// The postcondition is proven of the result; it is never supposed to excuse the
// operation that computes it. Supposing `result > x` of `x + 1` would rule out
// the wrap and discharge the obligation from the claim itself. The twin with
// `expects (x < INT_MAX)` is successor_above in fixtures/signed_arithmetic.cpp.
verified int successor_claimed_above(int x)
    ensures (result > x)
{
    return x + 1;
}

int main() {
    return successor_claimed_above(1) - 2;
}
