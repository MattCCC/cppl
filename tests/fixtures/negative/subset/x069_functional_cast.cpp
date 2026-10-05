// SPEC: CONSTRUCT-069
// RFC 0022: the refused twin of subset/x069_functional_cast.cpp (functional cast), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

using Byte = unsigned char;
verified unsigned probe(unsigned short x)
    ensures (result == x)
{
    return Byte(x);
}

int main() { return probe(2) == 2u ? 0 : 1; }
