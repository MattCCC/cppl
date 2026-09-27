// SPEC: CONSTRUCT-023
// RFC 0022, the V1 verified subset: a verified body may use this construct, dereference,
// and it is modeled. Its refused twin is negative/subset/x023_dereference.cpp.

verified unsigned probe(const unsigned* p)
    expects (readable(p))
    ensures (result == result)
{
    return *p;
}

int main() {
    const unsigned value = 2u;
    return probe(&value) == 2u ? 0 : 1;
}
