// SPEC: ARITH-009, BOUNDARYEX-001
// `x - 1` on the right of `&&` runs wherever `x < 0` holds, the least value
// included, so the left does not protect it. The twin testing `x > INT_MIN`
// first is stepped_if_small in fixtures/signed_arithmetic.cpp.
verified int stepped_if_negative(int x)
    ensures (result == x || result < -5)
{
    if (x < 0 && x - 1 < -5)
        return x - 1;
    return x;
}

int main() {
    return stepped_if_negative(0);
}
