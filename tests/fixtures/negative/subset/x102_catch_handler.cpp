// SPEC: CONSTRUCT-102
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// catch handler, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    try {
        return x;
    } catch (int) {
        return x;
    }
}

int main() { return probe(2u) == 2u ? 0 : 1; }
