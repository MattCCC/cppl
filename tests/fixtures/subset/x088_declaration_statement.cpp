// SPEC: CONSTRUCT-088
// RFC 0022, the V1 verified subset: a verified body may use this construct, declaration statement,
// and it is modeled. Its refused twin is negative/subset/x088_declaration_statement.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x + x)
{
    unsigned doubled = x + x;
    return doubled;
}

int main() {
    return probe(2u) == 4u ? 0 : 1;
}
