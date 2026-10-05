// SPEC: CONSTRUCT-051
// RFC 0022: the refused twin of subset/x051_compound_assignment.cpp (compound assignment), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x + 3u)
{
    unsigned total = x;
    total += 2u;
    return total;
}

int main() { return probe(2u) == 5u ? 0 : 1; }
