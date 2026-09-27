// SPEC: CONSTRUCT-037
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// bitwise xor, is refused.

verified unsigned probe(unsigned x)
    ensures (result == (x ^ 1u))
{
    return x ^ 1u;
}

int main() { return probe(2u) == 3u ? 0 : 1; }
