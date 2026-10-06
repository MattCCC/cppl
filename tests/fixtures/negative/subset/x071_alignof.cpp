// SPEC: CONSTRUCT-071
// RFC 0022: the refused twin of subset/x071_alignof.cpp (alignof), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned long probe()
    ensures (result == 1ul)
{
    return alignof(unsigned);
}

int main() { return probe() == 4ul ? 0 : 1; }
