// A proof is instantiated at a term of the type it quantifies over, and at
// nothing else (SPEC.md VERIFIED-023, CASE-013). The path is contradictory on
// its own, so only the argument's type refuses this.
proof pinned(unsigned v) proves (v == v)
{
    refl;
}

verified unsigned wrong_type(int x)
    expects (x < 0)
    ensures (result == 0u)
{
    if (x >= 0) {
        contradiction pinned(x);
    }
    return 0u;
}

int main() {
    return 0;
}
