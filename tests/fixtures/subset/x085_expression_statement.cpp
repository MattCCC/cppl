// SPEC: CONSTRUCT-085
// RFC 0022, the V1 verified subset: a verified body may use this construct, expression statement,
// and it is modeled. Its refused twin is negative/subset/x085_expression_statement.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x + 1u)
{
    unsigned counter = x;
    counter = counter + 1u;
    return counter;
}

int main() {
    return probe(2u) == 3u ? 0 : 1;
}
