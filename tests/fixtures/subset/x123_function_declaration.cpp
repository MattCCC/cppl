// SPEC: CONSTRUCT-123
// RFC 0022, the V1 verified subset: a verified body may use this construct, function declaration,
// and it is modeled. Its refused twin is negative/subset/x123_function_declaration.cpp.

verified unsigned probe(unsigned x)
    ensures (result == x);

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return x;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
