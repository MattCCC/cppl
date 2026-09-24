// SPEC: GHOST-002
// Ghost state cannot select which element runtime code reads.
verified unsigned pick(unsigned x)
    expects (x < 3u)
    ensures (result < 10u)
{
    unsigned values[3] = {1u, 2u, 3u};
    ghost unsigned g = x;
    return values[g];
}

int main() {
    return static_cast<int>(pick(1u));
}
