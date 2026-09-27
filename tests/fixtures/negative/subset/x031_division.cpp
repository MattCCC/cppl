// SPEC: CONSTRUCT-031
// RFC 0022: the refused twin of subset/x031_division.cpp (division), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned a, unsigned b)
    ensures (result == a / b)
{
    return a / b;
}

int main() { return probe(7u, 2u) == 3u ? 0 : 1; }
