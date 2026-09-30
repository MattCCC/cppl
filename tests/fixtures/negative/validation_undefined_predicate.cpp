// SPEC: RUNTIMECHECK-020, ARITH-010
// Testing the greatest int against this predicate would overflow, so no value
// may be validated against it.
type Below = int where (self + 1 > 0);

verified int below_or_one(int raw)
    ensures (result > -1)
{
    if (validate<Below>(raw)) {
        return 1;
    }
    return 1;
}

int main() {
    return below_or_one(5);
}
