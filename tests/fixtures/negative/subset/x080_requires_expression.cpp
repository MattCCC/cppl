// SPEC: CONSTRUCT-080
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// requires expression, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    constexpr bool addable = requires(unsigned a) { a + 1u; };
    if (addable) {
        return x;
    }
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
