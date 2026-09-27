// SPEC: CONSTRUCT-125
// RFC 0022: the refused twin of subset/x125_defaulted_function.cpp (defaulted function), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

struct Pair {
    unsigned first;
    unsigned second;

    bool operator==(const Pair&) const = default;
};

verified unsigned probe(Pair pair)
    ensures (result == pair.first)
{
    return pair.second;
}

int main() { return probe(Pair{2u, 3u}) == 2u ? 0 : 1; }
