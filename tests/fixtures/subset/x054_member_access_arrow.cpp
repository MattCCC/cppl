// SPEC: CONSTRUCT-054
// RFC 0022, the V1 verified subset: a verified body may use this construct, member access arrow,
// and it is modeled. Its refused twin is negative/subset/x054_member_access_arrow.cpp.

struct Pair {
    unsigned first;
    unsigned second;
};

verified unsigned probe(const Pair* pair)
    expects (readable(pair))
    ensures (result == result)
{
    return pair->first;
}

int main() {
    const Pair pair{2u, 3u};
    return probe(&pair) == 2u ? 0 : 1;
}
