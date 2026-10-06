// SPEC: CONSTRUCT-070
// RFC 0022: the refused twin of subset/x070_sizeof.cpp (sizeof), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned long probe()
    ensures (result == 8ul)
{
    return sizeof(unsigned);
}

int main() { return probe() == 4ul ? 0 : 1; }
