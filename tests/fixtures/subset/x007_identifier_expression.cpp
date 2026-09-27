// SPEC: CONSTRUCT-007
// RFC 0022, the V1 verified subset: a verified body may use this construct, identifier expression,
// and it is modeled. Its refused twin is negative/subset/x007_identifier_expression.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return x;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
