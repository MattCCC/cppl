// SPEC: CONSTRUCT-045
// RFC 0022: the refused twin of subset/x045_logical_and_runtime.cpp (logical and runtime), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result <= 10u)
{
    if (x > 0u || x < 10u) {
        return x;
    }
    return 0u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
