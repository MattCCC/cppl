// SPEC: CONSTRUCT-078
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// lambda expression, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    auto same = [](unsigned value) { return value; };
    (void)same;
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
