// SPEC: CONSTRUCT-047
// RFC 0022, the V1 verified subset: a verified body may use this construct, logical conjunction formal,
// and it is modeled. Its refused twin is negative/subset/x047_logical_conjunction_formal.cpp.

verified unsigned probe(unsigned x)
    expects (x < 100u)
    ensures (result >= x && result <= x + 1u)
{
    return x + 1u;
}

int main() {
    return probe(2u) == 3u ? 0 : 1;
}
