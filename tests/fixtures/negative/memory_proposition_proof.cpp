// SPEC: TRUSTED-003, VERIFIED-044
// A proof whose claim is a memory proposition proves nothing the kernel can
// check. Whatever its body says, it is refused for what it claims.
trusted law device_window(unsigned* registers)
    proves (readable(registers));

proof claims_a_capability(unsigned* registers)
    proves (readable(registers))
{
    exact device_window(registers);
}

int main() {
    return 0;
}
