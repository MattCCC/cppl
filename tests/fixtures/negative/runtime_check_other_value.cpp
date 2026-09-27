// SPEC: RUNTIMECHECK-004
// The check is of one value and the crossing of another. Twin of
// `positive_or_one`.
type Positive = int where (self > 0);

verified int other_value(int checked, int entered)
    ensures (result > 0)
{
    if (checked <= 0) {
        return 1;
    }
    Positive p = entered;
    return p;
}

int main() {
    return other_value(5, -5);
}
