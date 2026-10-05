// SPEC: CONSTRUCT-091
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// switch statement, is refused.

verified unsigned probe(unsigned x)
    ensures (result <= 2u)
{
    switch (x) {
    case 0u:
        return 0u;
    case 1u:
        return 1u;
    default:
        return 2u;
    }
}

int main() { return probe(1u) == 1u ? 0 : 1; }
