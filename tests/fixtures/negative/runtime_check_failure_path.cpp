// SPEC: RUNTIMECHECK-007, EDGECASE-079
// The refined value is constructed on the path where the check failed. Twin of
// `percentage_or_zero` in `fixtures/runtime_validation.cpp`.
type Percentage = int where (self >= 0 && self <= 100);

verified int failure_path(int raw)
    ensures (result >= 0 && result <= 100)
{
    if (raw >= 0 && raw <= 100) {
        return 0;
    }
    Percentage p = raw;
    return p;
}

int main() {
    return failure_path(500);
}
