// SPEC: CONSTRUCT-053
// RFC 0022, the V1 verified subset: a verified body may use this construct, member access dot,
// and it is modeled. Its refused twin is negative/subset/x053_member_access_dot.cpp.

struct Pair {
    unsigned first;
    unsigned second;
};

verified unsigned probe(Pair pair)
    ensures (result == pair.first)
{
    return pair.first;
}

int main() {
    return probe(Pair{2u, 3u}) == 2u ? 0 : 1;
}
