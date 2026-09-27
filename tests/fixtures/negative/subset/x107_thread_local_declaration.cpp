// SPEC: CONSTRUCT-107
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// thread_local declaration, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    thread_local unsigned calls = 0u;
    (void)calls;
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
