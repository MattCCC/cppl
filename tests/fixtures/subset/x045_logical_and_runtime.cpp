// SPEC: CONSTRUCT-045
// RFC 0022, the V1 verified subset: a verified body may use this construct, logical and runtime,
// and it is modeled. Its refused twin is negative/subset/x045_logical_and_runtime.cpp.

verified unsigned probe(unsigned x)
    ensures (result <= 10u)
{
    if (x > 0u && x < 10u) {
        return x;
    }
    return 0u;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
