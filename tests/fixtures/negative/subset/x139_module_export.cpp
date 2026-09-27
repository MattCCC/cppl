// SPEC: CONSTRUCT-139
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// module export, is refused.

export module probe_exports;

export {
    verified unsigned probe(unsigned x)
        ensures (result == x)
    {
        return x;
    }
}
