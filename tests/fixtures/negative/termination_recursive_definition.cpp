// SPEC: TERMINATION-002, CORRECT-005
// A recursive function terminates by its measure, and its contract may be
// called; it is never a definition the formal core unfolds, so a law that would
// evaluate it is refused rather than given a computation to run.
verified pure unsigned down(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return down(n - 1u);
}

law unfolds(unsigned n)
    proves (down(n) == 0u);

int main() {
    return static_cast<int>(down(3u));
}
