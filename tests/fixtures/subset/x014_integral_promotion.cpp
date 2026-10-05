// SPEC: CONSTRUCT-014
// RFC 0022, the V1 verified subset: a verified body may use this construct, integral promotion,
// and it is modeled. Its refused twin is negative/subset/x014_integral_promotion.cpp.

verified int probe(unsigned char a, unsigned char b)
    ensures (result == a + b)
{
    return a + b;
}

int main() {
    return probe(2, 3) == 5 ? 0 : 1;
}
