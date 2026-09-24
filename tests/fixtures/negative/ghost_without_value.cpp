// SPEC: GHOST-001
// Ghost state holds the value it is declared with, and nothing that runs may
// write it later, so one declared without a value would hold nothing.
verified unsigned empty(unsigned x)
    ensures (result == x)
{
    ghost unsigned g;
    return x;
}

int main() {
    return static_cast<int>(empty(2u));
}
