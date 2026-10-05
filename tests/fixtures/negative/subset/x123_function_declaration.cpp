// SPEC: CONSTRUCT-123
// RFC 0022: the refused twin of subset/x123_function_declaration.cpp (function declaration), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x + 1u);

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
