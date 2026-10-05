// SPEC: CONSTRUCT-033
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// left shift, is refused.

verified unsigned probe(unsigned x)
    expects (x < 16u)
    ensures (result == x * 2u)
{
    return x << 1;
}

int main() { return probe(2u) == 4u ? 0 : 1; }
