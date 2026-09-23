// SPEC: ERASE-003, WORD-001
// `old(x)` in a postcondition is specified and not implemented. Outside what is
// implemented `old` is an ordinary name, and there is none, so the contract is
// refused rather than dropped: a postcondition nothing states would read as a
// checked one.
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
