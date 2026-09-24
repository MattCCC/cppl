// SPEC: TRUSTED-003, VERIFIED-044
// The refused half of a matched pair whose accepted half is `device_window` in
// `fixtures/trust_closure.cpp`: the same memory proposition without `trusted`.
// No proof can establish a capability, so only an explicit assumption may state
// one.
law device_window(unsigned* registers, unsigned count)
    expects (count <= 64u)
    proves (readable(registers, count));

int main() {
    return 0;
}
