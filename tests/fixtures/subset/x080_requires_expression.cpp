// SPEC: CONSTRUCT-080
// RFC 0022, the V1 verified subset: a verified body may use this construct, requires expression,
// and it is modeled. Its refused twin is negative/subset/x080_requires_expression.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    constexpr bool addable = requires(unsigned a) { a + 1u; };
    if (addable) {
        return x;
    }
    return x + 1u;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
