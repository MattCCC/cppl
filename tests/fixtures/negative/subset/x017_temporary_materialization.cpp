// SPEC: CONSTRUCT-017
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// temporary materialization, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x + 1u)
{
    const unsigned& bound = x + 1u;
    return bound;
}

int main() { return probe(2u) == 3u ? 0 : 1; }
