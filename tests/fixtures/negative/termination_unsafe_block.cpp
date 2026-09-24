// SPEC: TERMINATION-006, UNSAFE-003
// An unsafe block need not return, so a function that passes through one does
// not terminate by anything verified.
unsafe void wait_for_device() {
}

verified unsigned polls(unsigned n)
    ensures (result == n)
    decreases (n)
{
    unsafe {
        wait_for_device();
    }
    return n;
}

int main() {
    return static_cast<int>(polls(4u));
}
