// SPEC: CONSTRUCT-129
// RFC 0022, the V1 verified subset: a verified body may use this construct, attribute specifier,
// and it is modeled. Its refused twin is negative/subset/x129_attribute_specifier.cpp.

[[nodiscard]] verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return x;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
