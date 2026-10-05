// SPEC: CONSTRUCT-145
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// move assignment, is refused.

#include <utility>

struct Pair {
    unsigned first;
    unsigned second;
};

verified unsigned probe(Pair pair)
    ensures (result == pair.first)
{
    Pair copy{0u, 0u};
    copy = std::move(pair);
    return copy.first;
}

int main() { return probe(Pair{2u, 3u}) == 2u ? 0 : 1; }
