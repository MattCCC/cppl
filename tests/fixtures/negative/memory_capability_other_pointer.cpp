// SPEC: VERIFIED-043
// A capability names the storage one pointer designates. Holding it for `held`
// says nothing about what `other` designates.
verified void touch(unsigned* p)
    expects (writable(p))
{
    *p = 7u;
}

verified void caller(unsigned* held, unsigned* other)
    expects (writable(held))
{
    touch(other);
}

int main() {
    return 0;
}
