// SPEC: CONSTRUCT-014
// RFC 0022: the refused twin of subset/x014_integral_promotion.cpp (integral promotion), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned char probe(unsigned char a, unsigned char b)
    ensures (result == a + b)
{
    return a + b;
}

int main() { return probe(2, 3) == 5 ? 0 : 1; }
