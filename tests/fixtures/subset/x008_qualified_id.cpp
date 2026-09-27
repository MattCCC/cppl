// SPEC: CONSTRUCT-008
// RFC 0022, the V1 verified subset: a verified body may use this construct, qualified-id,
// and it is modeled. Its refused twin is negative/subset/x008_qualified_id.cpp.

namespace inner {
verified unsigned same(unsigned x)
    ensures (result == x)
{
    return x;
}
} // namespace inner

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return inner::same(x);
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
