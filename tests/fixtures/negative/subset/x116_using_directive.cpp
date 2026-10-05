// SPEC: CONSTRUCT-116
// RFC 0022: the refused twin of subset/x116_using_directive.cpp (using directive), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

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
    return same(x) + 1u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
