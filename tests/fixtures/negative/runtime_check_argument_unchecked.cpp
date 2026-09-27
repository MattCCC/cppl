// SPEC: RUNTIMECHECK-007, REFINE-026
// The argument enters the callee's refined parameter on the path where the
// check failed. Twin of `checked_argument`.
type Positive = int where (self > 0);

verified int positive_identity(Positive p)
    ensures (result == p)
{
    return p;
}

verified int unchecked_argument(int raw)
    ensures (result > 0)
{
    if (raw > 0) {
        return 1;
    }
    return positive_identity(raw);
}

int main() {
    return unchecked_argument(-2);
}
