// SPEC: ARITH-007
// C++ truncates toward zero: -7 / 2 is -3, never the floor -4. The twin
// claiming -3 is negative_quotient.
verified int floored_quotient(int x)
    expects (x == -7)
    ensures (result == -4)
{
    return x / 2;
}

int main() {
    return floored_quotient(-7) + 3;
}
