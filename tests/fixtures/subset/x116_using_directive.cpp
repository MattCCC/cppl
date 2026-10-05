// SPEC: CONSTRUCT-116
// RFC 0022, the V1 verified subset: a verified body may use this construct, using directive,
// and it is modeled. Its refused twin is negative/subset/x116_using_directive.cpp.

namespace billing {
verified unsigned same(unsigned x)
    ensures (result == x)
{
    return x;
}
} // namespace billing

using namespace billing;

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return same(x);
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
