// SPEC: CONSTRUCT-072
// RFC 0022: the refused twin of subset/x072_decltype.cpp (decltype), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    decltype(x) copy = x + 1u;
    return copy;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
