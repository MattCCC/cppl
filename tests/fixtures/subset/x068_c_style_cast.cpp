// SPEC: CONSTRUCT-068
// RFC 0022, the V1 verified subset: a verified body may use this construct, C-style cast,
// and it is modeled. Its refused twin is negative/subset/x068_c_style_cast.cpp.

verified unsigned probe(unsigned short x)
    ensures (result == x)
{
    return (unsigned)x;
}

int main() {
    return probe(2) == 2u ? 0 : 1;
}
