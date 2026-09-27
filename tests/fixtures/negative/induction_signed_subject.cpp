// SPEC: ERASE-002, WORD-002
// `induction` is read so that its arms can be laid out, and this
// implementation's formal core has no induction rule (SPEC.md 21). A proof
// that uses it is refused, never erased as though it had been checked.
pure unsigned identity(unsigned x) {
    return x;
}

law identity_holds(unsigned x)
    proves (identity(x) == x);

proof identity_holds_by_induction(unsigned x)
    proves (identity_holds(x))
{
    induction x;
}

int main() {
    return static_cast<int>(identity(0u));
}
