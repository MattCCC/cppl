// SPEC: CONSTRUCT-114
// RFC 0022: the refused twin of subset/x114_namespace_alias.cpp (namespace alias), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

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
    return pay::same(x) + 1u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
