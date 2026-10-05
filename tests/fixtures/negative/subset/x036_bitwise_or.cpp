// SPEC: CONSTRUCT-036
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// bitwise or, is refused.

verified unsigned probe(unsigned x)
    ensures (result >= 1u)
{
    return x | 1u;
}

int main() { return probe(2u) == 3u ? 0 : 1; }
