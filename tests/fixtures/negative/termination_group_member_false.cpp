// SPEC: TERMINATION-007
// The functions of one recursion are established together or not at all, since
// each one's proof supposes the others' contracts. `twin` does not satisfy its
// contract, so `down`, whose proof supposed it, is not established either, and
// neither is `outside`, which rests on `down`.
unsigned twin(unsigned n);

verified unsigned down(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return twin(n - 1u);
}

verified unsigned twin(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 1u;
    }
    return down(n - 1u);
}

verified unsigned outside(unsigned n)
    ensures (result == 0u)
{
    return down(n);
}

int main() {
    return static_cast<int>(outside(2u));
}
