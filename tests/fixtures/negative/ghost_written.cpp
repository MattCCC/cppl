// SPEC: GHOST-002
// Runtime code cannot write ghost state: the write would run, into nothing.
verified unsigned rewrites(unsigned x)
    ensures (result == x)
{
    ghost unsigned g = x;
    g = 5u;
    return x;
}

int main() {
    return static_cast<int>(rewrites(2u));
}
