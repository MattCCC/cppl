// SPEC: ARITH-006, ARITH-009, DEFINEDBEHAVIOR-001
// `y < x` tests the result of `x + 1` after it was computed. At `INT_MAX` the
// addition has already overflowed, so the test cannot protect it, and the
// wrapped value it would see must not discharge the obligation. The contract
// holds of the ring value at every `x`; only the definedness fails. The twin
// whose precondition excludes `INT_MAX` is successor_or_zero in
// fixtures/signed_arithmetic.cpp.
verified int successor_tested_after(int x)
    ensures (result >= x || result == 0)
{
    int y = x + 1;
    if (y < x)
        return 0;
    return y;
}

int main() {
    return successor_tested_after(1) - 2;
}
