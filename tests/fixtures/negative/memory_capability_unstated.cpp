// SPEC: VERIFIED-043, VERIFIED-037
// The caller knows its pointer is not null and states nothing else. Non-nullness
// is not a capability, so the call cannot pass one on.
verified void touch(unsigned* p)
    expects (writable(p))
{
    *p = 7u;
}

verified void caller(unsigned* q)
    expects (q != nullptr)
{
    touch(q);
}

int main() {
    return 0;
}
