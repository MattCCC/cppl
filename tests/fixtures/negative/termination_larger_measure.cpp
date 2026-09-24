// SPEC: TERMINATION-005
// A recursive call at a larger measure climbs rather than descends, even where
// something else, here the bound on `n`, would end the recursion.
verified unsigned climbs(unsigned n)
    expects (n < 100u)
    ensures (result == 0u)
    decreases (n)
{
    if (n >= 99u) {
        return 0u;
    }
    return climbs(n + 1u);
}

int main() {
    return static_cast<int>(climbs(1u));
}
