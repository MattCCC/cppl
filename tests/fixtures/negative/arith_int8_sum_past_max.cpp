// SPEC: ARITH-008
// Two `signed char` values are added in `int`, where the sum always fits, and
// converting 128 back to `signed char` does not fit. The twin bounding the sum
// by 127 is int8_sum_within.
verified signed char int8_sum_past_max(signed char a, signed char b)
    expects (a + b <= 128 && a + b >= -128)
    ensures (result == a + b)
{
    return a + b;
}

int main() {
    return int8_sum_past_max(1, 2) - 3;
}
