// SPEC: RUNTIMECHECK-019, WORD-013
// A validation expression is checked only in the body of a verified function.
type Positive = int where (self > 0);

int positive_or_zero(int raw) {
    return validate<Positive>(raw) ? raw : 0;
}

int main() {
    return positive_or_zero(5);
}
