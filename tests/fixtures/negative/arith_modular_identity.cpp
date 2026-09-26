// SPEC: ARITH-006, ARITH-010, DEFINEDBEHAVIOR-001
// `(x + 1) - 1 == x` is an identity of the modular ring for every `x`, and the
// kernel's normal form decides it so. It is not a fact about signed `int`: at
// `INT_MAX` the addition is undefined. So neither a body computing `x + 1` nor a
// specification stating `x + 1 - 1 == x` is justified by the identity; each
// needs `x + 1` representable. The twins with `expects (x < INT_MAX)` are
// predecessor_of_successor and successor_cancels in fixtures/signed_arithmetic.cpp.
verified int successor_by_identity(int x)
    ensures (result - 1 == x)
{
    return x + 1;
}

verified int cancelled_by_identity(int x)
    ensures (x + 1 - 1 == x)
{
    return x;
}

int main() {
    return successor_by_identity(1) - 2 + cancelled_by_identity(0);
}
