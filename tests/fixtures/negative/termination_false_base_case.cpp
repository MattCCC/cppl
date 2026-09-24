// SPEC: TERMINATION-007, CORRECT-003
// Descent justifies supposing the contract at the recursive call, not the
// contract itself: the base case still owes it, and returns 0 where 1 is
// claimed. Recursion is not induction on the claim.
verified unsigned one(unsigned n)
    ensures (result == 1u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return one(n - 1u);
}

int main() {
    return static_cast<int>(one(3u));
}
