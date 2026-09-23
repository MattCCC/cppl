// An assumption that cannot be stated to the formal core cannot be used either
// (SPEC.md TRUSTED-005, TRUST.md 2.10).
//
// Signed addition is not modeled, so `signed_growth` has no proposition. A proof
// naming it is refused rather than given an assumption no one could read.
trusted law signed_growth(int x)
    proves (x + 1 != x);

proof uses_signed_growth(int y)
    proves (y == y)
{
    exact signed_growth(y);
}

int main() {
    return 0;
}
