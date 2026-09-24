// SPEC: GHOST-002
// Ghost state cannot be passed to a function that runs.
verified unsigned identity(unsigned x)
    ensures (result == x)
{
    return x;
}

verified unsigned passes(unsigned x)
    ensures (result == x)
{
    ghost unsigned g = x;
    return identity(g);
}

int main() {
    return static_cast<int>(passes(2u));
}
