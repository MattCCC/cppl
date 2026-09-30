// SPEC: RUNTIMECHECK-011, RUNTIMECHECK-013
// Refused twin of `validated_percentage` in fixtures/runtime_validation.cpp:
// the validation tested a weaker refinement than the one the value enters, and
// what it established does not imply the stronger predicate.
type Percentage = int where (self >= 0 && self <= 100);
type Positive = int where (self > 0);

verified int validated_percentage(int raw)
    ensures (result >= 0 && result <= 100)
{
    if (validate<Positive>(raw)) {
        Percentage p = raw;
        return p;
    }
    return 0;
}

int main() {
    return validated_percentage(5);
}
