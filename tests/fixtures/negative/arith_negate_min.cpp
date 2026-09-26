// SPEC: ARITH-006, DEFINEDBEHAVIOR-001, EXPR-007
// `-INT_MIN` is 2^31, which `int` does not hold. The twin that excludes
// `INT_MIN` is negated.
verified int negated_anything(int x)
    ensures (result == -x)
{
    return -x;
}

int main() {
    return negated_anything(-5) - 5;
}
