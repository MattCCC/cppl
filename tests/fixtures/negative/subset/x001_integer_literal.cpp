// SPEC: CONSTRUCT-001
// RFC 0022: the refused twin of subset/x001_integer_literal.cpp (integer literal), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x + 2u)
{
    return x + 1u;
}

int main() { return probe(2u) == 3u ? 0 : 1; }
