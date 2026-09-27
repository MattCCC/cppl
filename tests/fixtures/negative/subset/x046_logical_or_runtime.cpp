// SPEC: CONSTRUCT-046
// RFC 0022: the refused twin of subset/x046_logical_or_runtime.cpp (logical or runtime), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result >= 1u)
{
    if (x == 0u && x > 100u) {
        return 1u;
    }
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
