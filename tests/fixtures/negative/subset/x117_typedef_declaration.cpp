// SPEC: CONSTRUCT-117
// RFC 0022: the refused twin of subset/x117_typedef_declaration.cpp (typedef declaration), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

typedef unsigned Count;

verified Count probe(Count x)
    ensures (result == x)
{
    return x + 1u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
