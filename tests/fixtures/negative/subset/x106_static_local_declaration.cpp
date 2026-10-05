// SPEC: CONSTRUCT-106
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// static local declaration, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    static unsigned calls = 0u;
    (void)calls;
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
