// SPEC: INDUCT-005
// `x != 0` is preserved by every successor step below the maximum, and false at
// 0. The zero case must still be proved, so neither form closes it.
law never_zero(unsigned x)
    proves (x != 0u);

proof never_zero_by_induction(unsigned x)
    proves (never_zero(x))
{
    induction x;
}

int main() {
    return 0;
}
