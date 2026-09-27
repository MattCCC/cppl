// SPEC: CONSTRUCT-032
// RFC 0022: the refused twin of subset/x032_remainder.cpp (remainder), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned a)
    ensures (result < 2u)
{
    return a % 3u;
}

int main() { return probe(7u) == 1u ? 0 : 1; }
