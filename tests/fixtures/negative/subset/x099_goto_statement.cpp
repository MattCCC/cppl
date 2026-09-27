// SPEC: CONSTRUCT-099
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// goto statement, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    goto done;
done:
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
