// SPEC: TRUSTED-003, TRUSTED-008
// A trusted law admitting a memory proposition is an explicit assumption, but a
// capability is not a proposition a proof goal can be, so no statement can name
// it as evidence. Naming it is refused rather than read as something it is not.
trusted law device_window(unsigned* registers)
    proves (readable(registers));

proof borrows_the_window(unsigned x)
    proves (x == x)
{
    exact device_window;
}

int main() {
    return 0;
}
