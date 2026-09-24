// SPEC: GHOST-001, GHOST-002
// A ghost reference would alias runtime storage.
verified unsigned aliases(unsigned x)
    ensures (result == x)
{
    unsigned y = x;
    ghost unsigned& r = y;
    return x;
}

int main() {
    return static_cast<int>(aliases(2u));
}
