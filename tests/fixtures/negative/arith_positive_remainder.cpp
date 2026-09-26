// SPEC: ARITH-007
// The remainder takes the dividend's sign: -7 % 2 is -1, never 1. The twin
// claiming -1 is negative_remainder.
verified int positive_remainder(int x)
    expects (x == -7)
    ensures (result == 1)
{
    return x % 2;
}

int main() {
    return positive_remainder(-7) + 1;
}
