// SPEC: ERASE-003, WORD-001
// `old(x)` in a postcondition is specified and not implemented, so the
// contract is refused rather than dropped: a postcondition nothing states would
// read as a checked one. Whether any `old` is visible makes no difference
// (negative/old_shadowed_by_function.cpp).
verified void bump(unsigned& x)
    expects (x < 10u)
    ensures (x == old(x) + 1u)
{
    x = x + 1u;
}

int main() {
    unsigned value = 0u;
    bump(value);
    return static_cast<int>(value);
}
