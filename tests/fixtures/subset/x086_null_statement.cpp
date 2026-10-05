// SPEC: CONSTRUCT-086
// RFC 0022, the V1 verified subset: a verified body may use this construct, null statement,
// and it is modeled. Its refused twin is negative/subset/x086_null_statement.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    ;
    return x;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
