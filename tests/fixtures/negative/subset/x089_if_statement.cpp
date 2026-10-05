// SPEC: CONSTRUCT-089
// RFC 0022: the refused twin of subset/x089_if_statement.cpp (if statement), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result <= 10u)
{
    if (x > 10u) {
        return 11u;
    } else {
        return x;
    }
}

int main() { return probe(2u) == 2u ? 0 : 1; }
