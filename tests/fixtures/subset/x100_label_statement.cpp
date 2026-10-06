// SPEC: CONSTRUCT-100
// RFC 0022, the V1 verified subset: a verified body may use this construct, label statement,
// and it is modeled. Its refused twin is negative/subset/x100_label_statement.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
here:
    return x;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
