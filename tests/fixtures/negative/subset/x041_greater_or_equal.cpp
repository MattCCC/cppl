// SPEC: CONSTRUCT-041
// RFC 0022: the refused twin of subset/x041_greater_or_equal.cpp (greater-or-equal), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned a, unsigned b)
    ensures (result == 1u)
{
    if (a >= b) {
        return 1u;
    }
    return 0u;
}

int main() { return probe(1u, 1u) == 1u ? 0 : 1; }
