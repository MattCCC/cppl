// SPEC: CONSTRUCT-098
// RFC 0022: the refused twin of subset/x098_return_statement.cpp (return statement), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x + 1u)
{
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
