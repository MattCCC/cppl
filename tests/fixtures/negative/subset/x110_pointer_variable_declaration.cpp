// SPEC: CONSTRUCT-110
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// pointer variable declaration, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    const unsigned* none = nullptr;
    (void)none;
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
