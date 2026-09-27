// SPEC: CONSTRUCT-018
// RFC 0022: the refused twin of subset/x018_unary_plus.cpp (unary plus), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return +x + 1u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
