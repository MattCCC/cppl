// SPEC: CONSTRUCT-004
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// string literal, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    const char* text = "text";
    (void)text;
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
