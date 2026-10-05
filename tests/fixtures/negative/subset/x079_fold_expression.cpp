// SPEC: CONSTRUCT-079
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// fold expression, is refused.

template <typename... Values>
verified unsigned probe(Values... values)
    ensures (result == result)
{
    return (0u + ... + values);
}

int main() { return probe(1u, 2u) == 3u ? 0 : 1; }
