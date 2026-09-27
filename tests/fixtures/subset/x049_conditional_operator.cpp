// SPEC: CONSTRUCT-049
// RFC 0022, the V1 verified subset: a verified body may use this construct, conditional operator,
// and it is modeled. Its refused twin is negative/subset/x049_conditional_operator.cpp.

verified unsigned probe(unsigned x)
    ensures (result >= 5u)
{
    return x > 5u ? x : 5u;
}

int main() {
    return probe(2u) == 5u ? 0 : 1;
}
