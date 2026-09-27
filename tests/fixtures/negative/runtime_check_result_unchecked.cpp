// SPEC: RUNTIMECHECK-007
// The refined result is returned on the path where the check failed. Twin of
// `checked_result`.
type Positive = int where (self > 0);

verified Positive unchecked_result(int raw) {
    if (raw > 0) {
        return 1;
    }
    return raw;
}

int main() {
    return unchecked_result(-2);
}
