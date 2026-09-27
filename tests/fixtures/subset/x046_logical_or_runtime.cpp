// SPEC: CONSTRUCT-046
// RFC 0022, the V1 verified subset: a verified body may use this construct, logical or runtime,
// and it is modeled. Its refused twin is negative/subset/x046_logical_or_runtime.cpp.

verified unsigned probe(unsigned x)
    ensures (result >= 1u)
{
    if (x == 0u || x > 100u) {
        return 1u;
    }
    return x;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
