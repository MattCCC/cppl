// A claim's evidence is a proof the kernel admitted (SPEC.md VERIFIED-023,
// CASE-013). This proof's own body does not establish what it claims, so it is
// refused, and a claim naming it has no evidence, even though the path is
// contradictory on its own.
proof false_claim(unsigned v) proves (v == 0u)
{
    refl;
}

verified unsigned leans_on_it(unsigned x)
    expects (x < 5u)
    ensures (result == x)
{
    if (x >= 5u) {
        contradiction false_claim(x);
    }
    return x;
}

int main() {
    return 0;
}
