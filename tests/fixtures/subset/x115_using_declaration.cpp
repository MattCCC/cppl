// SPEC: CONSTRUCT-115
// RFC 0022, the V1 verified subset: a verified body may use this construct, using declaration,
// and it is modeled. Its refused twin is negative/subset/x115_using_declaration.cpp.

namespace billing {
verified unsigned same(unsigned x)
    ensures (result == x)
{
    return x;
}
} // namespace billing

using billing::same;

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return same(x);
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
