// SPEC: CONSTRUCT-076
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// delete expression, is refused.

verified unsigned probe(unsigned* cell, unsigned x)
    ensures (result == x)
{
    delete cell;
    return x;
}

int main() { return probe(new unsigned(1u), 2u) == 2u ? 0 : 1; }
