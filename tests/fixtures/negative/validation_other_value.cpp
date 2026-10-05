// SPEC: RUNTIMECHECK-011, RUNTIMECHECK-013
// Refused twin of `validated_or_one` in fixtures/runtime_validation.cpp: the
// validation tested `raw`, and its fact describes that value only, not another
// that crosses on the same path.
type Positive = int where (self > 0);

verified int validated_or_one(int raw, int other)
    ensures (result > 0)
{
    if (validate<Positive>(raw)) {
        Positive p = other;
        return p;
    }
    return 1;
}

int main() {
    return validated_or_one(5, 0);
}
