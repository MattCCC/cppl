// SPEC: UNSAFE-002, PURE-005
// A pure function is one the formal core may unfold. One holding an unsafe
// block has effects nothing checked, so it is not pure, and a law about it
// cannot be stated.
pure unsigned pure_with_block(unsigned a) {
    unsigned b = 0u;
    unsafe {
        b = 1u;
    }
    return a;
}

law returns_argument(unsigned a)
    proves (pure_with_block(a) == a);

int main() {
    return static_cast<int>(pure_with_block(0u));
}
