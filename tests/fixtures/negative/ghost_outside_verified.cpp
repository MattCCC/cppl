// SPEC: GHOST-001
// Ghost state exists for a verified body's proof. In an ordinary function
// nothing would check how it is used.
unsigned ordinary(unsigned x) {
    ghost unsigned g = x;
    return x;
}

int main() {
    return static_cast<int>(ordinary(2u));
}
