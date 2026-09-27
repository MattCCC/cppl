// SPEC: CONSTRUCT-066
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// const_cast, is refused.

verified unsigned probe(const unsigned* p)
    expects (readable(p))
    ensures (result == result)
{
    unsigned* writable_view = const_cast<unsigned*>(p);
    return *writable_view;
}

int main() {
    unsigned value = 2u;
    return probe(&value) == 2u ? 0 : 1;
}
