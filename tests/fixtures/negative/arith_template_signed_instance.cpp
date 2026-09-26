// SPEC: ARITH-003, ARITH-006, ARITH-009
// One written `a + b`, verified per specialization at the common type that
// specialization gives it. At `int` it owes representability, which nothing
// here establishes. The twin instantiated at `unsigned` and `unsigned long
// long`, where the sum wraps and owes nothing, is wrapped_sum in
// fixtures/signed_arithmetic.cpp.
template <typename T>
verified T wrapped_sum(T a, T b)
    ensures (result == a + b)
{
    return a + b;
}

int main() {
    return wrapped_sum<int>(1, 2) - 3;
}
