// SPEC: CONSTRUCT-130
// RFC 0022: the refused twin of subset/x130_alignas_specifier.cpp (alignas specifier), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    alignas(16) unsigned copy = x;
    return copy + 1u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
