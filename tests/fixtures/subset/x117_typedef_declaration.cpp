// SPEC: CONSTRUCT-117
// RFC 0022, the V1 verified subset: a verified body may use this construct, typedef declaration,
// and it is modeled. Its refused twin is negative/subset/x117_typedef_declaration.cpp.

typedef unsigned Count;

verified Count probe(Count x)
    ensures (result == x)
{
    return x;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
