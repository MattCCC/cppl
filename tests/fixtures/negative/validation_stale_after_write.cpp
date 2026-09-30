// SPEC: RUNTIMECHECK-012, REFINE-023
// Refused twin of `revalidated` in fixtures/runtime_validation.cpp: a write
// gives the local a version the validation said nothing of.
type Positive = int where (self > 0);

verified int revalidated(int raw)
    ensures (result > 0)
{
    int value = raw;
    if (!validate<Positive>(value)) {
        return 1;
    }
    value = value - 1;
    Positive p = value;
    return p;
}

int main() {
    return revalidated(5);
}
