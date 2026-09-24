// SPEC: UNSAFE-003, VERIFIED-013, VERIFIED-043
// A verified call after an unsafe block owes its callee's capability like any
// other call, and the caller no longer holds it.
verified void touch(unsigned* p)
    expects (writable(p))
{
    *p = 7u;
}

verified void calls_after(unsigned* q)
    expects (writable(q))
{
    unsafe {
        q[0] = 1u;
    }
    touch(q);
}

int main() {
    unsigned cell = 0u;
    calls_after(&cell);
    return static_cast<int>(cell);
}
