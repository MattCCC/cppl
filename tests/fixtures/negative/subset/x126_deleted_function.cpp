// SPEC: CONSTRUCT-126
// RFC 0022: the refused twin of subset/x126_deleted_function.cpp (deleted function), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

unsigned narrow(double) = delete;

verified unsigned narrow(unsigned x)
    ensures (result == x)
{
    return x;
}

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return narrow(x) + 1u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
