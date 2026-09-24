// SPEC: LOOP-001, LOOP-003
// What follows a `do` loop runs where its condition fails, and owes the
// contract from there: this return claims one more than the loop leaves.
verified unsigned last(unsigned n)
    expects (n > 0u)
    ensures (result == 0u)
{
    unsigned i = n;
    do
        invariant (i > 0u)
        decreases (i)
    {
        i = i - 1u;
    } while (i > 0u);
    return i + 1u;
}

int main() {
    return static_cast<int>(last(3u));
}
