// SPEC: CONSTRUCT-057
// RFC 0022, the V1 verified subset: a verified body may use this construct, direct function call,
// and it is modeled. Its refused twin is negative/subset/x057_direct_function_call.cpp.

verified unsigned successor(unsigned x)
    expects (x < 100u)
    ensures (result == x + 1u)
{
    return x + 1u;
}

verified unsigned probe(unsigned x)
    expects (x < 50u)
    ensures (result == x + 1u)
{
    return successor(x);
}

int main() {
    return probe(2u) == 3u ? 0 : 1;
}
