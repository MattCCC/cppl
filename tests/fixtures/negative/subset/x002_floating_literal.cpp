// SPEC: CONSTRUCT-002
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// floating literal, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    double scale = 1.5;
    (void)scale;
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
