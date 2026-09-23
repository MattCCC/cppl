// The evidence a claim names establishes an equality or a comparison, which is
// what linear arithmetic reasons from (SPEC.md VERIFIED-023, CASE-013). This
// proof establishes a quantified proposition, so it states no contradiction,
// even though the path is contradictory on its own.
proof everything_equals_itself() proves (forall (unsigned y) { y == y })
{
    refl;
}

verified unsigned quantified(unsigned x)
    expects (x < 5u)
    ensures (result == x)
{
    if (x >= 5u) {
        contradiction everything_equals_itself;
    }
    return x;
}

int main() {
    return 0;
}
