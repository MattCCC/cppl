// SPEC: CONSTRUCT-001
// RFC 0022, the V1 verified subset: a verified body may use this construct, integer literal,
// and it is modeled. Its refused twin is negative/subset/x001_integer_literal.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x + 1u)
{
    return x + 1u;
}

int main() {
    return probe(2u) == 3u ? 0 : 1;
}
