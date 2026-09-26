// SPEC: ARITH-006, DEFINEDBEHAVIOR-001
// Two `unsigned short` values are promoted to `int`, where 65535 * 65535 does
// not fit: the classic product that overflows although both operands are
// unsigned. The twins multiplying `unsigned char` and `short` values, whose
// products always fit, are int8_product and int16_product.
verified int unsigned_short_product(unsigned short a, unsigned short b)
    ensures (result == a * b)
{
    return a * b;
}

int main() {
    return unsigned_short_product(2, 3) - 6;
}
