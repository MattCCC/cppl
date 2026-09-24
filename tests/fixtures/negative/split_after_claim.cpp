// SPEC: CASE-019
// A claim ends the arm's path, so a statement after it in that arm is never
// reached and is refused rather than silently dropped.
enum class Mode : unsigned { idle = 0u, busy = 1u };

proof same(unsigned x)
    proves (x == x)
{
    refl;
}

verified unsigned after_claim(Mode m)
    expects (m != Mode::idle)
    ensures (result == 0u)
{
    cases m {
        Mode::idle => {
            contradiction same(0u);
            cases m {
                Mode::idle => {}

                Mode::busy => {}

                unnamed(value) => {}
            }
        }

        Mode::busy => {}

        unnamed(value) => {}
    }
    return 0u;
}

int main() {
    return 0;
}
