// SPEC: ARITH-007, DEFINEDBEHAVIOR-003
// A nonzero divisor is not enough: `INT_MIN / -1` is 2^31, which `int` does
// not hold. The twin that also excludes `INT_MIN` is divided_safely.
verified int divided_by_nonzero(int x, int y)
    expects (y != 0)
    ensures (result == x / y)
{
    return x / y;
}

int main() {
    return divided_by_nonzero(4, 2) - 2;
}
