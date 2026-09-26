// SPEC: ARITH-007, DEFINEDBEHAVIOR-002
// `y >= 0` admits zero. The twin with `y > 0` is divided.
verified int divided_by_nonnegative(int x, int y)
    expects (y >= 0)
    ensures (result == x / y)
{
    return x / y;
}

int main() {
    return divided_by_nonnegative(4, 2) - 2;
}
