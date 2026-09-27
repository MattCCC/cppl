// SPEC: CONSTRUCT-005
// RFC 0022, the V1 verified subset: a verified body may use this construct, boolean literal,
// and it is modeled. Its refused twin is negative/subset/x005_boolean_literal.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    bool flag = true;
    if (flag) {
        return x;
    }
    return 0u;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
