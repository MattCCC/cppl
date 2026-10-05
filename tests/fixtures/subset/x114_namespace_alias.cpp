// SPEC: CONSTRUCT-114
// RFC 0022, the V1 verified subset: a verified body may use this construct, namespace alias,
// and it is modeled. Its refused twin is negative/subset/x114_namespace_alias.cpp.

namespace billing {
verified unsigned same(unsigned x)
    ensures (result == x)
{
    return x;
}
} // namespace billing

namespace pay = billing;

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return pay::same(x);
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
