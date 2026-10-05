// SPEC: CONSTRUCT-008
// RFC 0022: the refused twin of subset/x008_qualified_id.cpp (qualified-id), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

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
    return inner::same(x) + 1u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
