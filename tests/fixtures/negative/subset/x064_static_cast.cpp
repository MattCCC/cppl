// SPEC: CONSTRUCT-064
// RFC 0022: the refused twin of subset/x064_static_cast.cpp (static_cast), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned short x)
    ensures (result == x)
{
    return static_cast<unsigned char>(x);
}

int main() { return probe(2) == 2u ? 0 : 1; }
