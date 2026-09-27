// SPEC: CONSTRUCT-081
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// pack expansion, is refused.

verified unsigned sum(unsigned a, unsigned b)
    ensures (result == a + b)
{
    return a + b;
}

template <typename... Values>
verified unsigned probe(Values... values)
    ensures (result == result)
{
    return sum(values...);
}

int main() { return probe(1u, 2u) == 3u ? 0 : 1; }
