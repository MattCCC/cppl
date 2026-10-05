// SPEC: CONSTRUCT-089
// RFC 0022, the V1 verified subset: a verified body may use this construct, if statement,
// and it is modeled. Its refused twin is negative/subset/x089_if_statement.cpp.

verified unsigned probe(unsigned x)
    ensures (result <= 10u)
{
    if (x > 10u) {
        return 10u;
    } else {
        return x;
    }
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
