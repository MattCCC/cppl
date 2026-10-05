// SPEC: CONSTRUCT-137
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// module declaration, is refused.

export module probe_module;

export verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return x;
}
