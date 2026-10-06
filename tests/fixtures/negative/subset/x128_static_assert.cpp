// SPEC: CONSTRUCT-128
// RFC 0022: the refused twin of subset/x128_static_assert.cpp (static_assert), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    static_assert(sizeof(unsigned) >= 64, "a 512-bit unsigned at least");
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
