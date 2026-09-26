// SPEC: ARITH-007, DEFINEDBEHAVIOR-003
// `INT_MIN % -1` is undefined as well, although its value would be 0: C++
// defines the remainder only where the quotient is representable. The twin that
// excludes -1 is remainder_safely.
verified int remainder_by_nonzero(int x, int y)
    expects (y != 0)
    ensures (result == x % y)
{
    return x % y;
}

int main() {
    return remainder_by_nonzero(7, 4) - 3;
}
