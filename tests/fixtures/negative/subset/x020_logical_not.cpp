// SPEC: CONSTRUCT-020
// RFC 0022: the refused twin of subset/x020_logical_not.cpp (logical not), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    if (!(x == 1u)) {
        return x;
    }
    return 0u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
