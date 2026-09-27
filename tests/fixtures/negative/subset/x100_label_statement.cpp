// SPEC: CONSTRUCT-100
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// label statement, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
here:
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
