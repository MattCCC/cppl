// SPEC: CONSTRUCT-016
// RFC 0022: the refused twin of subset/x016_qualification_conversion.cpp (qualification conversion), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    const unsigned& view = x;
    return view + 1u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
