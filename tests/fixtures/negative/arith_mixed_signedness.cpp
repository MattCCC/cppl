// SPEC: ARITH-008, CONSTRUCT-015
// Comparing an `int` with `0u` converts it to `unsigned`, so no value is below
// zero there and the first return is never taken: a negative `i` returns 1u.
// The twin claiming 1u is never_below_zero.
verified unsigned below_zero(int i)
    expects (i < 0)
    ensures (result == 0u)
{
    if (i < 0u)
        return 0u;
    return 1u;
}

int main() {
    return static_cast<int>(below_zero(-1));
}
