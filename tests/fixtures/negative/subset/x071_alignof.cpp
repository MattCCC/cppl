// SPEC: CONSTRUCT-071
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// alignof, is refused.

verified unsigned long probe()
    ensures (result >= 1ul)
{
    return alignof(unsigned);
}

int main() { return probe() >= 1ul ? 0 : 1; }
