// SPEC: RUNTIMECHECK-007
// The route where `a && b` fails is the union of `!a` and `a && !b`; the
// value may be the one the first operand rejected. Twin of
// `percentage_or_zero`.
type Positive = int where (self > 0);

verified int failed_conjunction(int raw)
    ensures (result > 0)
{
    if (raw <= 0 && raw > -100) {
        return 1;
    }
    Positive p = raw;
    return p;
}

int main() {
    return failed_conjunction(-500);
}
