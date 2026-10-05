// SPEC: RUNTIMECHECK-019, UNSAFE-003
// An unsafe block's statements are not a path the verifier walks, so a
// validation there would establish nothing that is checked.
type Positive = int where (self > 0);

verified int positive_or_one(int raw)
    ensures (result > 0)
{
    int value = 1;
    unsafe {
        if (validate<Positive>(raw)) {
            value = raw;
        }
    }
    return value;
}

int main() {
    return positive_or_one(5);
}
