// SPEC: CONSTRUCT-115
// RFC 0022: the refused twin of subset/x115_using_declaration.cpp (using declaration), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

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
    return same(x) + 1u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
