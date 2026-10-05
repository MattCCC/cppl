// SPEC: CONSTRUCT-111
// RFC 0022: the refused twin of subset/x111_array_declaration.cpp (array declaration), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    unsigned values[2] = {x, 0u};
    return values[1];
}

int main() { return probe(2u) == 2u ? 0 : 1; }
