// SPEC: CONSTRUCT-075
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// new expression, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    unsigned* cell = new unsigned(x);
    (void)cell;
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
