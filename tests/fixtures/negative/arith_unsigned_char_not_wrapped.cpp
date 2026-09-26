// SPEC: ARITH-003, ARITH-006
// Two `unsigned char` operands promote to `int`, which holds all their values,
// so `a + b` is a signed `int` addition: 255 + 255 is 510, not the 254 that
// 8-bit wrapping would give. A contract claiming the wrapped value is refused.
// The twin stating the `int` sum is unsigned_char_sum in
// fixtures/signed_arithmetic.cpp.
verified int unsigned_char_wrapped(unsigned char a, unsigned char b)
    expects (a == 255 && b == 255)
    ensures (result == 254)
{
    return a + b;
}

int main() {
    return unsigned_char_wrapped(255, 255) == 510 ? 0 : 1;
}
