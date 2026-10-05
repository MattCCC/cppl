// SPEC: CONSTRUCT-026
// RFC 0022: the refused twin of subset/x026_prefix_decrement.cpp (prefix decrement), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x - 2u)
{
    unsigned counter = x;
    --counter;
    return counter;
}

int main() { return probe(2u) == 1u ? 0 : 1; }
