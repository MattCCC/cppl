// SPEC: CONSTRUCT-020
// RFC 0022, the V1 verified subset: a verified body may use this construct, logical not,
// and it is modeled. Its refused twin is negative/subset/x020_logical_not.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    if (!(x == 0u)) {
        return x;
    }
    return 0u;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
