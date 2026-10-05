// SPEC: CONSTRUCT-134
// RFC 0022: the refused twin of subset/x134_explicit_instantiation.cpp (explicit instantiation), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

template <unsigned N>
verified unsigned probe(unsigned x)
    ensures (result == x + N)
{
    return x + N + 1u;
}

template unsigned probe<4u>(unsigned);

int main() { return probe<4u>(2u) == 6u ? 0 : 1; }
