// SPEC: CONSTRUCT-041
// RFC 0022, the V1 verified subset: a verified body may use this construct, greater-or-equal,
// and it is modeled. Its refused twin is negative/subset/x041_greater_or_equal.cpp.

verified unsigned probe(unsigned a, unsigned b)
    expects (a >= b)
    ensures (result == 1u)
{
    if (a >= b) {
        return 1u;
    }
    return 0u;
}

int main() {
    return probe(1u, 1u) == 1u ? 0 : 1;
}
