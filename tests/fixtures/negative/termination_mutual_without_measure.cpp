// SPEC: TERMINATION-007
// Every function of a recursion states a measure; one that does not leaves an
// edge of the cycle with nothing to descend.
unsigned odd_steps(unsigned n);

verified unsigned even_steps(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return odd_steps(n - 1u);
}

verified unsigned odd_steps(unsigned n)
    ensures (result == 0u)
{
    if (n == 0u) {
        return 0u;
    }
    return even_steps(n - 1u);
}

int main() {
    return static_cast<int>(even_steps(4u));
}
