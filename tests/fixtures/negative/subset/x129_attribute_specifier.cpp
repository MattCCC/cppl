// SPEC: CONSTRUCT-129
// RFC 0022: the refused twin of subset/x129_attribute_specifier.cpp (attribute specifier), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

[[nodiscard]] verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return x + 1u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
