// SPEC: BOUNDARYEX-010, UNSAFE-005
// The unsafe call executes, but produces no range proposition: that the reading
// is always in range still needs validation or trust. The same function whose
// result is only the runtime comparison, with no claim that it holds, is ordinary
// runtime behavior.
unsafe unsigned read_device();

verified bool always_in_range()
    ensures (result)
{
    unsigned x = 0u;
    unsafe {
        x = read_device();
    }
    return x <= 100u;
}

unsafe unsigned read_device() {
    return 42u;
}

int main() {
    return always_in_range() ? 0 : 1;
}
