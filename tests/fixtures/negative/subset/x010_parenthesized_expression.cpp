// SPEC: CONSTRUCT-010
// RFC 0022: the refused twin of subset/x010_parenthesized_expression.cpp (parenthesized expression), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return (x + 1u);
}

int main() { return probe(2u) == 2u ? 0 : 1; }
