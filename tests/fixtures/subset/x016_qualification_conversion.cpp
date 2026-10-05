// SPEC: CONSTRUCT-016
// RFC 0022, the V1 verified subset: a verified body may use this construct, qualification conversion,
// and it is modeled. Its refused twin is negative/subset/x016_qualification_conversion.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    const unsigned& view = x;
    return view;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
