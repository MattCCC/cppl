// SPEC: CONSTRUCT-050
// RFC 0022, the V1 verified subset: a verified body may use this construct, assignment,
// and it is modeled. Its refused twin is negative/subset/x050_assignment.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    unsigned copy = 0u;
    copy = x;
    return copy;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
