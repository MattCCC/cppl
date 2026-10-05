// SPEC: CONSTRUCT-103
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// throw expression, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    if (x > 1000u) {
        throw 1;
    }
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
