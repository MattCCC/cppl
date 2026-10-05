// SPEC: CONSTRUCT-034
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// right shift, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x / 2u)
{
    return x >> 1;
}

int main() { return probe(4u) == 2u ? 0 : 1; }
