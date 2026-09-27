// SPEC: RUNTIMECHECK-010
// The loop's condition held inside it; after the loop only its failure is
// known, and the carried value is one no invariant bounds. Twin of
// `last_small`.
type Small = unsigned where (self < 10u);

verified unsigned after_loop(unsigned n)
    ensures (result < 10u)
{
    unsigned i = 0u;
    while (i < n)
        decreases (n - i)
    {
        i = i + 1u;
    }
    Small s = i;
    return s;
}

int main() {
    return static_cast<int>(after_loop(20u));
}
