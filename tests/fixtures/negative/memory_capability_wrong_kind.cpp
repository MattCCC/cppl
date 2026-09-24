// SPEC: VERIFIED-038, VERIFIED-043
// The refused half of a matched pair whose accepted half is `holds` in
// `fixtures/memory_capabilities.cpp`: the caller may read its buffer and the
// callee writes one. `readable` does not entail `writable`.
verified void touch(unsigned* p)
    expects (writable(p))
{
    *p = 7u;
}

verified void caller(unsigned* q)
    expects (readable(q))
{
    touch(q);
}

int main() {
    return 0;
}
