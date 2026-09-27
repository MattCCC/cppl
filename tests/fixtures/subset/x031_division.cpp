// SPEC: CONSTRUCT-031
// RFC 0022, the V1 verified subset: a verified body may use this construct, division,
// and it is modeled. Its refused twin is negative/subset/x031_division.cpp.

verified unsigned probe(unsigned a, unsigned b)
    expects (b != 0u)
    ensures (result == a / b)
{
    return a / b;
}

int main() {
    return probe(7u, 2u) == 3u ? 0 : 1;
}
