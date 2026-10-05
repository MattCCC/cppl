// SPEC: CONSTRUCT-048
// RFC 0022: the refused twin of subset/x048_logical_disjunction_formal.cpp (logical disjunction formal), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == 0u || result == x)
{
    if (x > 5u) {
        return 1u;
    }
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
