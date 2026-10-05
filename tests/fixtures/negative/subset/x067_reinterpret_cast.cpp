// SPEC: CONSTRUCT-067
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// reinterpret_cast, is refused.

verified unsigned probe(const unsigned* p)
    expects (readable(p))
    ensures (result == result)
{
    const int* view = reinterpret_cast<const int*>(p);
    (void)view;
    return *p;
}

int main() {
    const unsigned value = 2u;
    return probe(&value) == 2u ? 0 : 1;
}
