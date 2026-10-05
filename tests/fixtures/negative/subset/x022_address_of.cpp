// SPEC: CONSTRUCT-022
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// address-of, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    unsigned copy = x;
    unsigned* where = &copy;
    (void)where;
    return copy;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
