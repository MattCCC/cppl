// SPEC: GHOST-001
// A static local outlives the call, which is state of the program, not of one
// proof.
verified unsigned keeps(unsigned x)
    ensures (result == x)
{
    ghost static unsigned g = 1u;
    return x;
}

int main() {
    return static_cast<int>(keeps(2u));
}
