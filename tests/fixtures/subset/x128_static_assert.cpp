// SPEC: CONSTRUCT-128
// RFC 0022, the V1 verified subset: a verified body may use this construct, static_assert,
// and it is modeled. Its refused twin is negative/subset/x128_static_assert.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    static_assert(sizeof(unsigned) >= 2, "a 16-bit unsigned at least");
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
