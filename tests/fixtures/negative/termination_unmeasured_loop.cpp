// SPEC: TERMINATION-006, CORRECT-004
// A `decreases` clause makes termination part of the claim, so every loop the
// function runs needs a measure: this one may run forever.
verified unsigned spin(unsigned n)
    ensures (result == n)
    decreases (n)
{
    unsigned i = 0u;
    while (i != n)
        invariant (true)
    {
        i = i + 2u;
    }
    return i;
}

int main() {
    return static_cast<int>(spin(4u));
}
