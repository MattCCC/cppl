// A claim names a proof declaration as its evidence (SPEC.md VERIFIED-023,
// CASE-013). A name that no proof declares is reported where it is written, and
// the claim is never taken as established.
verified unsigned named(unsigned x)
    ensures (result == x)
{
    if (x > x) {
        contradiction nowhere;
    }
    return x;
}

int main() {
    return 0;
}
