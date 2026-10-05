// SPEC: CONSTRUCT-035
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// bitwise and, is refused.

verified unsigned probe(unsigned x)
    ensures (result <= 1u)
{
    return x & 1u;
}

int main() { return probe(3u) == 1u ? 0 : 1; }
