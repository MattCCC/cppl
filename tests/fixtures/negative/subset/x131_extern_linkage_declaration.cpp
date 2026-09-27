// SPEC: CONSTRUCT-131
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// extern linkage declaration, is refused.

extern "C" {
verified unsigned cppl_probe(unsigned x)
    ensures (result == x)
{
    return x;
}
}

int main() { return cppl_probe(2u) == 2u ? 0 : 1; }
