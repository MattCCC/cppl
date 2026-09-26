// SPEC: ARITH-008
// Two `short` values are added in `int`, and -32769 does not fit `short`. The
// twin bounding the sum by -32768 is int16_sum_within.
verified short int16_sum_past_min(short a, short b)
    expects (a + b <= 32767 && a + b >= -32769)
    ensures (result == a + b)
{
    return a + b;
}

int main() {
    return int16_sum_past_min(1, 2) - 3;
}
