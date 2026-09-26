// SPEC: ARITH-010
// A law claimed at an argument is the law of that argument's value, and `x + 1`
// has none where it overflows. Nothing supposes it does not, so the claim is
// refused rather than given the wrapped value.
law reflexive(int y)
    proves (y == y);

proof reflexive_at_successor(int x)
    proves (reflexive(x + 1))
{
    refl;
}

int main() {
    return 0;
}
