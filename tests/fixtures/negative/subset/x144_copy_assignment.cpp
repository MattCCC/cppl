// SPEC: CONSTRUCT-144
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// copy assignment, is refused.

struct Pair {
    unsigned first;
    unsigned second;
};

verified unsigned probe(Pair pair)
    ensures (result == pair.first)
{
    Pair copy{0u, 0u};
    copy = pair;
    return copy.first;
}

int main() { return probe(Pair{2u, 3u}) == 2u ? 0 : 1; }
