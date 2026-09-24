// SPEC: GHOST-002
// A lambda is code that runs, so it cannot capture ghost state either.
verified unsigned captures(unsigned x)
    ensures (result == x)
{
    ghost unsigned g = x;
    unsigned y = x;
    [&y, g]() { y = g; }();
    return x;
}

int main() {
    return static_cast<int>(captures(2u));
}
