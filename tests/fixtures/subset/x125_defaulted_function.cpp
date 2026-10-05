// SPEC: CONSTRUCT-125
// RFC 0022, the V1 verified subset: a verified body may use this construct, defaulted function,
// and it is modeled. Its refused twin is negative/subset/x125_defaulted_function.cpp.

struct Pair {
    unsigned first;
    unsigned second;

    bool operator==(const Pair&) const = default;
};

verified unsigned probe(Pair pair)
    ensures (result == pair.first)
{
    return pair.first;
}

int main() {
    return probe(Pair{2u, 3u}) == 2u ? 0 : 1;
}
