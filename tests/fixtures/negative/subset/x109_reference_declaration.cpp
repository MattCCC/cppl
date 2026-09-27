// SPEC: CONSTRUCT-109
// RFC 0022: the refused twin of subset/x109_reference_declaration.cpp (reference declaration), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    unsigned target = 0u;
    unsigned alias = target;
    alias = x;
    return target;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
