// SPEC: CONSTRUCT-032
// RFC 0022, the V1 verified subset: a verified body may use this construct, remainder,
// and it is modeled. Its refused twin is negative/subset/x032_remainder.cpp.

verified unsigned probe(unsigned a)
    ensures (result < 3u)
{
    return a % 3u;
}

int main() {
    return probe(7u) == 1u ? 0 : 1;
}
