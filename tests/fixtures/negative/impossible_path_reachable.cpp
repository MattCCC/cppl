// The refused half of a matched pair (SPEC.md VERIFIED-023, CASE-015). The
// accepted half is `after` in `fixtures/impossible_path.cpp`: the same
// function, differing only in the branch condition. Here `x == 3` or `x == 4`
// reaches the claim, so it is not shown to be unreachable, and the return after
// it would break the postcondition if the claim were believed.
pure unsigned zero() {
    return 0u;
}

proof nothing() proves (zero() == 0u)
{
    refl;
}

verified unsigned after(unsigned x)
    expects (x < 5u)
    ensures (result < 5u)
{
    if (x >= 3u) {
        contradiction nothing;
        return 7u;
    }
    return x;
}

int main() {
    return 0;
}
