// SPEC: CONSTRUCT-112
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// structured binding, is refused.

struct Pair {
    unsigned first;
    unsigned second;
};

verified unsigned probe(Pair pair)
    ensures (result == pair.first)
{
    auto [first, second] = pair;
    (void)second;
    return first;
}

int main() { return probe(Pair{2u, 3u}) == 2u ? 0 : 1; }
