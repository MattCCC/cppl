// SPEC: ARITH-006, DEFINEDBEHAVIOR-001
// 2147484 * 1000 is 2147484000, one thousand past `INT_MAX`. The twin bounding
// `x` by 2147483 is int32_scaled.
verified int int32_scaled_too_far(int x)
    expects (x >= -2147483 && x <= 2147484)
    ensures (result == x * 1000)
{
    return x * 1000;
}

int main() {
    return int32_scaled_too_far(0);
}
