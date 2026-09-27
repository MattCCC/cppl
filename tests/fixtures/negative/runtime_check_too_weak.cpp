// SPEC: RUNTIMECHECK-002, RUNTIMECHECK-007
// The check admits zero, which the predicate does not: an off-by-one check
// establishes a weaker fact than the crossing owes. Twin of `positive_or_one`.
type Positive = int where (self > 0);

verified int off_by_one(int raw)
    ensures (result > 0)
{
    if (raw < 0) {
        return 1;
    }
    Positive p = raw;
    return p;
}

int main() {
    return off_by_one(0);
}
