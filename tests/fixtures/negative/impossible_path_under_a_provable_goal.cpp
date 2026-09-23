// A claim is about whether the path occurs, never about its goal (SPEC.md
// VERIFIED-023, CASE-005). The postcondition holds on every path, so returning
// here would be proven; the claim that no execution reaches this point is still
// false, since any `x > 3` does, and it is refused.
pure unsigned zero() {
    return 0u;
}

proof nothing() proves (zero() == 0u)
{
    refl;
}

verified unsigned anything(unsigned x)
    ensures (result == result)
{
    if (x > 3u) {
        contradiction nothing;
    }
    return x;
}

int main() {
    return 0;
}
