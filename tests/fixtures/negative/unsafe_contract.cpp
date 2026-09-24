// SPEC: UNSAFE-003, UNSAFE-004
// Nothing checks an unsafe function, so a contract written on one would be a
// fact its callers rest on that no proof and no trusted law states.
unsafe unsigned read_device()
    ensures (result < 10u);

verified unsigned uses()
    ensures (result < 10u)
{
    unsigned x = 0u;
    unsafe {
        x = read_device();
    }
    return x;
}

unsafe unsigned read_device() {
    return 42u;
}

int main() {
    return static_cast<int>(uses());
}
