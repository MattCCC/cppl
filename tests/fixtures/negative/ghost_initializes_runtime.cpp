// SPEC: GHOST-002
// Runtime values may be copied into ghost state, never the other way: a
// runtime object initialized from a ghost would hold nothing once it is erased.
struct Pair {
    unsigned first;
    unsigned second;
};

verified unsigned copies(unsigned x)
    ensures (result == x)
{
    ghost unsigned g = x;
    Pair pair{g, 1u};
    return pair.first;
}

int main() {
    return static_cast<int>(copies(2u));
}
