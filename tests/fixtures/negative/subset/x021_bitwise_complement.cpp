// SPEC: CONSTRUCT-021
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// bitwise complement, is refused.

verified unsigned probe(unsigned x)
    ensures (result == ~x)
{
    return ~x;
}

int main() { return probe(0u) == ~0u ? 0 : 1; }
