// SPEC: RUNTIMECHECK-004, REFINE-010
// The value checked is overwritten before it enters the refinement, so the
// check describes a version the crossing does not read. Twin of
// `positive_or_one`.
type Positive = int where (self > 0);

verified int stale_after_write(int raw)
    ensures (result > 0)
{
    int value = raw;
    if (value <= 0) {
        return 1;
    }
    value = value - 1;
    Positive p = value;
    return p;
}

int main() {
    return stale_after_write(1);
}
