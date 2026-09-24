// SPEC: GHOST-002
// Ghost state cannot control runtime branching.
verified unsigned choose(unsigned x)
    ensures (result <= 1u)
{
    ghost unsigned g = x;
    if (g > 3u) {
        return 1u;
    }
    return 0u;
}

int main() {
    return static_cast<int>(choose(4u));
}
