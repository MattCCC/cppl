// SPEC: GHOST-001
// As the body of an `if`, an erased ghost declaration would leave the `if`
// guarding whatever statement follows it.
verified unsigned guarded(unsigned x)
    ensures (result == x)
{
    if (x > 1u)
        ghost unsigned g = x;
    return x;
}

int main() {
    return static_cast<int>(guarded(2u));
}
