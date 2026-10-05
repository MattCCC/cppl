// SPEC: CONSTRUCT-011
// RFC 0022, the V1 verified subset: a verified body may use this construct, lvalue-to-rvalue conversion,
// and it is modeled. Its refused twin is negative/subset/x011_lvalue_to_rvalue_conversion.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    unsigned copy = x;
    return copy;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
