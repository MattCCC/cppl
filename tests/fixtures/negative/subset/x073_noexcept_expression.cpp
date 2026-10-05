// SPEC: CONSTRUCT-073
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// noexcept expression, is refused.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    if (noexcept(x + 1u)) {
        return x;
    }
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
