// SPEC: CONSTRUCT-105
// RFC 0022: the refused twin of subset/x105_local_variable_declaration.cpp (local variable declaration), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    unsigned local = x + 1u;
    return local;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
