// SPEC: CONSTRUCT-007
// RFC 0022: the refused twin of subset/x007_identifier_expression.cpp (identifier expression), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x + 1u)
{
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
