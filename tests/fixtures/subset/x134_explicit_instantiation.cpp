// SPEC: CONSTRUCT-134
// RFC 0022, the V1 verified subset: a verified body may use this construct, explicit instantiation,
// and it is modeled. Its refused twin is negative/subset/x134_explicit_instantiation.cpp.

template <unsigned N>
verified unsigned probe(unsigned x)
    ensures (result == x + N)
{
    return x + N;
}

template unsigned probe<4u>(unsigned);

int main() {
    return probe<4u>(2u) == 6u ? 0 : 1;
}
