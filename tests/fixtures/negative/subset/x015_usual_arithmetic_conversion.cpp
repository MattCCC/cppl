// SPEC: CONSTRUCT-015
// RFC 0022: the refused twin of subset/x015_usual_arithmetic_conversion.cpp (usual arithmetic conversion), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned a, unsigned long b)
    ensures (result == a + b)
{
    return a + b;
}

int main() { return probe(2u, 3ul) == 5ul ? 0 : 1; }
