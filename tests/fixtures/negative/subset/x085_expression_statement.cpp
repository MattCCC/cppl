// SPEC: CONSTRUCT-085
// RFC 0022: the refused twin of subset/x085_expression_statement.cpp (expression statement), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x + 1u)
{
    unsigned counter = x;
    counter = counter + 2u;
    return counter;
}

int main() { return probe(2u) == 3u ? 0 : 1; }
