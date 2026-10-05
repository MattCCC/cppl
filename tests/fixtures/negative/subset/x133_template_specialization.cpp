// SPEC: CONSTRUCT-133
// RFC 0022: the refused twin of subset/x133_template_specialization.cpp (template specialization), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

template <unsigned N>
verified unsigned probe(unsigned x)
    ensures (result == x + N)
{
    return x + N;
}

template <>
verified unsigned probe<0u>(unsigned x)
    ensures (result == x)
{
    return x + 1u;
}

int main() { return probe<0u>(2u) == 2u && probe<1u>(2u) == 3u ? 0 : 1; }
