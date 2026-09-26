// SPEC: ARITH-008
// 2147483648u does not fit `int`. The twin bounded by 2147483647u is
// from_unsigned.
verified int from_unsigned_one_past(unsigned u)
    expects (u <= 2147483648u)
    ensures (result >= 0)
{
    return u;
}

int main() {
    return from_unsigned_one_past(0u);
}
