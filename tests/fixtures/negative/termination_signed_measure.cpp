// SPEC: TERMINATION-005, 22.5
// A signed type has no least element a descent could stop at, so it is not a
// well-founded domain, and no bound is assumed for it.
verified int count(int n)
    ensures (result == 0)
    decreases (n)
{
    if (n <= 0) {
        return 0;
    }
    return count(n - 1);
}

int main() {
    return count(3);
}
