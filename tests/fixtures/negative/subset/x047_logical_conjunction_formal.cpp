// SPEC: CONSTRUCT-047
// RFC 0022: the refused twin of subset/x047_logical_conjunction_formal.cpp (logical conjunction formal), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    expects (x < 100u)
    ensures (result >= x && result < x + 1u)
{
    return x + 1u;
}

int main() { return probe(2u) == 3u ? 0 : 1; }
