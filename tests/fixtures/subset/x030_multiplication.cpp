// SPEC: CONSTRUCT-030
// RFC 0022, the V1 verified subset: a verified body may use this construct, multiplication,
// and it is modeled. Its refused twin is negative/subset/x030_multiplication.cpp.

verified unsigned probe(unsigned a)
    ensures (result == a * 3u)
{
    return a * 3u;
}

int main() {
    return probe(2u) == 6u ? 0 : 1;
}
