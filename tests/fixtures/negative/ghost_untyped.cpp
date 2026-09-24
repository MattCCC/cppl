// SPEC: GHOST-001
// Without a type, what follows `ghost` is an assignment to runtime state, not a
// declaration.
verified unsigned assigns(unsigned x)
    ensures (result == x)
{
    unsigned y = x;
    ghost y = 5u;
    return x;
}

int main() {
    return static_cast<int>(assigns(2u));
}
