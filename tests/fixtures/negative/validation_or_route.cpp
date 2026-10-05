// SPEC: RUNTIMECHECK-011, RUNTIMECHECK-013, RUNTIMECHECK-017
// Refused twin of `validated_below` in fixtures/runtime_validation.cpp: the
// branch is also taken when the validation failed and the other operand held,
// so on that route nothing was established about `raw`.
type Positive = int where (self > 0);

verified int validated_or_flag(int raw, bool flag)
    ensures (result > 0)
{
    if (validate<Positive>(raw) || flag) {
        Positive p = raw;
        return p;
    }
    return 1;
}

int main() {
    return validated_or_flag(5, false);
}
