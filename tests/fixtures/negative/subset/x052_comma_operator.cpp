// SPEC: CONSTRUCT-052
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// comma operator, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    unsigned copy = 0u;
    copy = (copy = 1u, x);
    return copy;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
