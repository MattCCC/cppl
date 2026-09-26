// An assumption that cannot be stated to the formal core cannot be used either
// (SPEC.md TRUSTED-005, TRUST.md 2.10).
//
// `doubled` adds two signed values, which C++ defines only where the sum fits,
// so it is not a total definition the core may unfold (SPEC.md ARITH-011), and
// `doubled_grows` has no proposition. A proof naming it is refused rather than
// given an assumption no one could read.
pure int doubled(int x) {
    return x + x;
}

trusted law doubled_grows(int x)
    proves (doubled(x) != x + 1);

proof uses_doubled_grows(int y)
    proves (y == y)
{
    exact doubled_grows(y);
}

int main() {
    return 0;
}
