// SPEC: CONSTRUCT-109
// RFC 0022, the V1 verified subset: a verified body may use this construct, reference declaration,
// and it is modeled. Its refused twin is negative/subset/x109_reference_declaration.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    unsigned target = 0u;
    unsigned& alias = target;
    alias = x;
    return target;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
