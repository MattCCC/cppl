// SPEC: CONSTRUCT-068
// RFC 0022: the refused twin of subset/x068_c_style_cast.cpp (C-style cast), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned short x)
    ensures (result == x)
{
    return (unsigned char)x;
}

int main() { return probe(2) == 2u ? 0 : 1; }
