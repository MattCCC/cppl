// SPEC: GHOST-001, GHOST-002
// A ghost that shadows a runtime local: the analysed program reads the ghost,
// the erased one would read the outer local. The use is refused rather than
// verified against a value the program never computes.
verified unsigned shadowed(unsigned x)
    ensures (result == 7u)
{
    unsigned y = x;
    {
        ghost unsigned y = 7u;
        return y;
    }
}

int main() {
    return static_cast<int>(shadowed(2u));
}
