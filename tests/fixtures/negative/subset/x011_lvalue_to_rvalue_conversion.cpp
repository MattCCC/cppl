// SPEC: CONSTRUCT-011
// RFC 0022: the refused twin of subset/x011_lvalue_to_rvalue_conversion.cpp (lvalue-to-rvalue conversion), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    unsigned copy = x;
    return copy + 1u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
