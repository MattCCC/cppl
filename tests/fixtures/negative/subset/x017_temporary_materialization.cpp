// SPEC: CONSTRUCT-017
// RFC 0022: the refused twin of subset/x017_temporary_materialization.cpp (temporary materialization), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x + 2u)
{
    const unsigned& bound = x + 1u;
    return bound;
}

int main() { return probe(2u) == 3u ? 0 : 1; }
