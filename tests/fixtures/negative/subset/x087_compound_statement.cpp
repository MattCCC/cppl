// SPEC: CONSTRUCT-087
// RFC 0022: the refused twin of subset/x087_compound_statement.cpp (compound statement), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    {
        unsigned inner = x + 1u;
        return inner;
    }
}

int main() { return probe(2u) == 2u ? 0 : 1; }
