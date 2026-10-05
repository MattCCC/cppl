// SPEC: CONSTRUCT-072
// RFC 0022, the V1 verified subset: a verified body may use this construct, decltype,
// and it is modeled. Its refused twin is negative/subset/x072_decltype.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    decltype(x) copy = x;
    return copy;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
