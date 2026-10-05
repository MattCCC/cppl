// SPEC: CONSTRUCT-090
// RFC 0022, the V1 verified subset: a verified body may use this construct, if constexpr,
// and it is modeled. Its refused twin is negative/subset/x090_if_constexpr.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    if constexpr (1u < 2u) {
        return x;
    } else {
        return 0u;
    }
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
