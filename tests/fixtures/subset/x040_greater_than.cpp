// SPEC: CONSTRUCT-040
// RFC 0022, the V1 verified subset: a verified body may use this construct, greater-than,
// and it is modeled. Its refused twin is negative/subset/x040_greater_than.cpp.

verified unsigned probe(unsigned a, unsigned b)
    expects (a > b)
    ensures (result == 1u)
{
    if (a > b) {
        return 1u;
    }
    return 0u;
}

int main() {
    return probe(2u, 1u) == 1u ? 0 : 1;
}
