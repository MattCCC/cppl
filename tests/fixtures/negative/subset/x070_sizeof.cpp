// SPEC: CONSTRUCT-070
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// sizeof, is refused.

verified unsigned long probe()
    ensures (result >= 1ul)
{
    return sizeof(unsigned);
}

int main() { return probe() >= 1ul ? 0 : 1; }
