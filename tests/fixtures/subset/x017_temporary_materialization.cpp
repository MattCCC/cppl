// SPEC: CONSTRUCT-017
// RFC 0022, the V1 verified subset: a verified body may use this construct, temporary materialization,
// and it is modeled. Its refused twin is negative/subset/x017_temporary_materialization.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x + 1u)
{
    const unsigned& bound = x + 1u;
    return bound;
}

int main() {
    return probe(2u) == 3u ? 0 : 1;
}
