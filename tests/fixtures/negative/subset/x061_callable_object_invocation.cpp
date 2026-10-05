// SPEC: CONSTRUCT-061
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// callable object invocation, is refused.

struct Identity {
    unsigned operator()(unsigned x) const {
        return x;
    }
};

verified unsigned probe(Identity identity, unsigned x)
    ensures (result == result)
{
    return identity(x);
}

int main() { return probe(Identity{}, 2u) == 2u ? 0 : 1; }
