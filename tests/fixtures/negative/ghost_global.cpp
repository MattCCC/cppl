// SPEC: GHOST-001
// Ghost parameters, members and globals are not part of the grammar
// (SPEC.md 25).
ghost unsigned counter = 0u;

verified unsigned identity(unsigned x)
    ensures (result == x)
{
    return x;
}

int main() {
    return static_cast<int>(identity(2u));
}
