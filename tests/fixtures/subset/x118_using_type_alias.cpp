// SPEC: CONSTRUCT-118
// RFC 0022, the V1 verified subset: a verified body may use this construct, using type alias,
// and it is modeled. Its refused twin is negative/subset/x118_using_type_alias.cpp.

using Count = unsigned;

verified Count probe(Count x)
    ensures (result == x)
{
    return x;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
