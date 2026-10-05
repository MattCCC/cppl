// SPEC: CONSTRUCT-012
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// array-to-pointer conversion, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    unsigned values[2] = {x, x};
    const unsigned* first = values;
    (void)first;
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
