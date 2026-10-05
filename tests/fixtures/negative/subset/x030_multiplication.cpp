// SPEC: CONSTRUCT-030
// RFC 0022: the refused twin of subset/x030_multiplication.cpp (multiplication), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned a)
    ensures (result == a * 2u)
{
    return a * 3u;
}

int main() { return probe(2u) == 6u ? 0 : 1; }
