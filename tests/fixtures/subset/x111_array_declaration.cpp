// SPEC: CONSTRUCT-111
// RFC 0022, the V1 verified subset: a verified body may use this construct, array declaration,
// and it is modeled. Its refused twin is negative/subset/x111_array_declaration.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    unsigned values[2] = {x, 0u};
    return values[0];
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
