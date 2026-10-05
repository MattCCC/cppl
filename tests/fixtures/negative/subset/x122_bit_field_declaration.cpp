// SPEC: CONSTRUCT-122
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// bit-field declaration, is refused.

struct Flags {
    unsigned ready : 1;
    unsigned rest : 31;
};

verified unsigned probe(Flags flags)
    ensures (result == result)
{
    return flags.ready;
}

int main() { return probe(Flags{1u, 0u}) == 1u ? 0 : 1; }
