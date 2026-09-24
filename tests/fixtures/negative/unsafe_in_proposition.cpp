// SPEC: UNSAFE-003, UNSAFE-004
// A proposition cannot rest on what an unsafe function returns: it is not a
// definition the formal core has, and no law admits a fact about it.
unsafe unsigned read_device() {
    return 42u;
}

law device_answers()
    proves (read_device() == 42u);

int main() {
    return static_cast<int>(read_device());
}
