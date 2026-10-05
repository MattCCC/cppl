// SPEC: CONSTRUCT-050
// RFC 0022: the refused twin of subset/x050_assignment.cpp (assignment), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    unsigned copy = 0u;
    copy = x + 1u;
    return copy;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
