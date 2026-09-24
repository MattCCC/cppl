// SPEC: CASE-006, CASE-018
// A binder names a value of its own, as in a proof body, so it never repeats a
// parameter's name: in the arm, the parameter could no longer be named at all.
enum class Mode : unsigned { idle = 0u, busy = 1u };

verified unsigned repeats(Mode m, unsigned value)
    ensures (result == 0u)
{
    cases m {
        Mode::idle => {
        }

        Mode::busy => {
        }

        unnamed(value) => {
        }
    }
    return 0u;
}

int main() {
    return 0;
}
