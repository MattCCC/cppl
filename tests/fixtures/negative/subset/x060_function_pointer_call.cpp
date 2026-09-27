// SPEC: CONSTRUCT-060
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// function pointer call, is refused.

unsigned identity(unsigned x) {
    return x;
}

verified unsigned probe(unsigned (*function)(unsigned), unsigned x)
    ensures (result == result)
{
    return function(x);
}

int main() { return probe(identity, 2u) == 2u ? 0 : 1; }
