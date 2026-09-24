// SPEC: CASE-019
// An arm continues its path, which has no goal, so a statement that closes one
// has nothing to close there.
enum class Mode : unsigned { idle = 0u, busy = 1u };

verified unsigned closes_a_goal(Mode m)
    ensures (result == 0u)
{
    cases m {
        Mode::idle => {
            refl;
        }

        Mode::busy => {}

        unnamed(value) => {}
    }
    return 0u;
}

int main() {
    return 0;
}
