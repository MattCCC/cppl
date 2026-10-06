// SPEC: CONSTRUCT-100
// RFC 0022: the refused twin of subset/x100_label_statement.cpp (label statement), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x + 1u)
{
here:
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
