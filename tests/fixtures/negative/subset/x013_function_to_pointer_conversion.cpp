// SPEC: CONSTRUCT-013
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// function-to-pointer conversion, is refused.

unsigned identity(unsigned x) {
    return x;
}

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    unsigned (*function)(unsigned) = identity;
    (void)function;
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
