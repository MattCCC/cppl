// SPEC: CONSTRUCT-015
// RFC 0022, the V1 verified subset: a verified body may use this construct, usual arithmetic conversion,
// and it is modeled. Its refused twin is negative/subset/x015_usual_arithmetic_conversion.cpp.

verified unsigned long probe(unsigned a, unsigned long b)
    ensures (result == a + b)
{
    return a + b;
}

int main() {
    return probe(2u, 3ul) == 5ul ? 0 : 1;
}
