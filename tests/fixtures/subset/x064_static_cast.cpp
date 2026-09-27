// SPEC: CONSTRUCT-064
// RFC 0022, the V1 verified subset: a verified body may use this construct, static_cast,
// and it is modeled. Its refused twin is negative/subset/x064_static_cast.cpp.

verified unsigned probe(unsigned short x)
    ensures (result == x)
{
    return static_cast<unsigned>(x);
}

int main() {
    return probe(2) == 2u ? 0 : 1;
}
