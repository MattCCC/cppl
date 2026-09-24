// SPEC: VERIFIED-038, VERIFIED-043
// The refused half of a matched pair whose accepted half is `holds_sized` in
// `fixtures/memory_capabilities.cpp`: the callee is asked to write one element
// more than the caller may.
verified void fill(unsigned* p, unsigned n)
    expects (writable(p, n))
{
    if (n > 1u) {
        p[1u] = 5u;
    }
}

verified void caller(unsigned* q, unsigned m)
    expects (writable(q, m))
{
    fill(q, m + 1u);
}

int main() {
    return 0;
}
