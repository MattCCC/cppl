// SPEC: CONSTRUCT-133
// RFC 0022, the V1 verified subset: a verified body may use this construct, template specialization,
// and it is modeled. Its refused twin is negative/subset/x133_template_specialization.cpp.

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
    return x;
}

int main() {
    return probe<0u>(2u) == 2u && probe<1u>(2u) == 3u ? 0 : 1;
}
