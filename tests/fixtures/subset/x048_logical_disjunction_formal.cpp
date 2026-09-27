// SPEC: CONSTRUCT-048
// RFC 0022, the V1 verified subset: a verified body may use this construct, logical disjunction formal,
// and it is modeled. Its refused twin is negative/subset/x048_logical_disjunction_formal.cpp.

verified unsigned probe(unsigned x)
    ensures (result == 0u || result == x)
{
    if (x > 5u) {
        return 0u;
    }
    return x;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
