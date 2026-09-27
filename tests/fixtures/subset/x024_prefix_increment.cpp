// SPEC: CONSTRUCT-024
// RFC 0022, the V1 verified subset: a verified body may use this construct, prefix increment,
// and it is modeled. Its refused twin is negative/subset/x024_prefix_increment.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x + 1u)
{
    unsigned counter = x;
    ++counter;
    return counter;
}

int main() {
    return probe(2u) == 3u ? 0 : 1;
}
