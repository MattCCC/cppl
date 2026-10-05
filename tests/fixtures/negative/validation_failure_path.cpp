// SPEC: RUNTIMECHECK-007, RUNTIMECHECK-011, RUNTIMECHECK-013
// Refused twin of `validated_percentage` in fixtures/runtime_validation.cpp:
// the value crosses on the path where the validation failed, which supposes
// nothing, and an unproven crossing is never made a validation site.
type Percentage = int where (self >= 0 && self <= 100);

verified int validated_percentage(int raw)
    ensures (result >= 0 && result <= 100)
{
    if (validate<Percentage>(raw)) {
        return 0;
    }
    Percentage p = raw;
    return p;
}

int main() {
    return validated_percentage(5);
}
