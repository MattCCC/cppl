// SPEC: CONSTRUCT-051
// RFC 0022, the V1 verified subset: a verified body may use this construct, compound assignment,
// and it is modeled. Its refused twin is negative/subset/x051_compound_assignment.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x + 3u)
{
    unsigned total = x;
    total += 3u;
    return total;
}

int main() {
    return probe(2u) == 5u ? 0 : 1;
}
