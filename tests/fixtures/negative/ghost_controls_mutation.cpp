// SPEC: GHOST-002
// Ghost state cannot decide how often runtime state changes: here it bounds the
// loop that writes `total`.
verified unsigned counts(unsigned n)
    ensures (result == n)
{
    ghost unsigned g = n;
    unsigned total = 0u;
    for (unsigned i = 0u; i < g; ++i)
        invariant (total == i && i <= n)
    {
        total = total + 1u;
    }
    return total;
}

int main() {
    return static_cast<int>(counts(3u));
}
