// SPEC: VERIFIED-038, VERIFIED-043
// The refused half of a matched pair whose accepted half is `narrower` in
// `fixtures/memory_capabilities.cpp`: without the guard, the caller's region
// may hold no element at all, so it cannot supply the one object `touch` writes.
verified void touch(unsigned* p)
    expects (writable(p))
{
    *p = 7u;
}

verified void caller(unsigned* q, unsigned m)
    expects (writable(q, m))
{
    touch(q);
}

int main() {
    return 0;
}
