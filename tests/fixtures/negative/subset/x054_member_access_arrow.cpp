// SPEC: CONSTRUCT-054
// RFC 0022: the refused twin of subset/x054_member_access_arrow.cpp (member access arrow), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

struct Pair {
    unsigned first;
    unsigned second;
};

verified unsigned probe(const Pair* pair)
    ensures (result == result)
{
    return pair->first;
}

int main() {
    const Pair pair{2u, 3u};
    return probe(&pair) == 2u ? 0 : 1;
}
