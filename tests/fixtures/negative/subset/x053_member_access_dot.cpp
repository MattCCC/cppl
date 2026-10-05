// SPEC: CONSTRUCT-053
// RFC 0022: the refused twin of subset/x053_member_access_dot.cpp (member access dot), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

struct Pair {
    unsigned first;
    unsigned second;
};

verified unsigned probe(Pair pair)
    ensures (result == pair.first)
{
    return pair.second;
}

int main() { return probe(Pair{2u, 3u}) == 2u ? 0 : 1; }
