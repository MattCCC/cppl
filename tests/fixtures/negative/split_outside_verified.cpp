// SPEC: WORD-012
// A split on a path is checked only as part of a verified function's
// verification. In any other function it would split nothing, so it is refused
// rather than accepted unchecked.
enum class Mode : unsigned { idle = 0u, busy = 1u };

unsigned outside(Mode m) {
    cases m {
        Mode::idle => {}

        Mode::busy => {}

        unnamed(value) => {}
    }
    return 0u;
}

int main() {
    return 0;
}
