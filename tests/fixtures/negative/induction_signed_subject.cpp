// SPEC: INDUCT-004, ERASE-002, WORD-002
// Signed integers are not generated from zero by successor below a maximum, so
// `induction` has no principle for them (SPEC.md 21.2). The proof is refused,
// never erased as though it had been checked, although the claim itself is
// true and provable without induction.
pure int identity(int x) {
    return x;
}

law identity_holds(int x)
    proves (identity(x) == x);

proof identity_holds_by_induction(int x)
    proves (identity_holds(x))
{
    induction x;
}

int main() {
    return identity(0);
}
