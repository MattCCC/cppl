// SPEC: CONSTRUCT-088
// RFC 0022: the refused twin of subset/x088_declaration_statement.cpp (declaration statement), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x + x)
{
    unsigned doubled = x + 1u;
    return doubled;
}

int main() { return probe(2u) == 4u ? 0 : 1; }
