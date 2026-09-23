// A proof is instantiated at no more terms than it quantifies over (SPEC.md
// VERIFIED-023, CASE-013). The path is contradictory on its own, so only the
// extra argument refuses this.
proof pinned(unsigned v) proves (v == v)
{
    refl;
}

verified unsigned extra(unsigned x)
    expects (x < 5u)
    ensures (result == x)
{
    if (x >= 5u) {
        contradiction pinned(x, x);
    }
    return x;
}

int main() {
    return 0;
}
