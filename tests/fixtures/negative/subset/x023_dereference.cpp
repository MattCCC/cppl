// SPEC: CONSTRUCT-023
// RFC 0022: the refused twin of subset/x023_dereference.cpp (dereference), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(const unsigned* p)
    ensures (result == result)
{
    return *p;
}

int main() {
    const unsigned value = 2u;
    return probe(&value) == 2u ? 0 : 1;
}
