// SPEC: CONSTRUCT-145
// RFC 0022, the V1 verified subset: a verified body may use this construct, move assignment,
// and it is modeled. Its refused twin is negative/subset/x145_move_assignment.cpp.

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

int main() {
    return probe(Pair{2u, 3u}) == 2u ? 0 : 1;
}
