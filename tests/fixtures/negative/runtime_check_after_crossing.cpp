// SPEC: RUNTIMECHECK-004, RUNTIMECHECK-005
// The check runs after the value entered the refinement, so it established
// nothing where the crossing stands. Twin of `positive_or_one`.
type Positive = int where (self > 0);

verified int checked_too_late(int raw)
    ensures (result > 0)
{
    Positive p = raw;
    if (raw <= 0) {
        return 1;
    }
    return p;
}

int main() {
    return checked_too_late(-3);
}
