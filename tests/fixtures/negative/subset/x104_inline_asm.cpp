// SPEC: CONSTRUCT-104
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// inline asm, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    asm volatile("" ::: "memory");
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
