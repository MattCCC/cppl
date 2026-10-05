// SPEC: CONSTRUCT-113
// RFC 0022: the refused twin of subset/x113_namespace_declaration.cpp (namespace declaration), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

namespace billing {
verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return x + 1u;
}
} // namespace billing

int main() { return billing::probe(2u) == 2u ? 0 : 1; }
