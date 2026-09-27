// SPEC: CONSTRUCT-105
// RFC 0022, the V1 verified subset: a verified body may use this construct, local variable declaration,
// and it is modeled. Its refused twin is negative/subset/x105_local_variable_declaration.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    unsigned local = x;
    return local;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
