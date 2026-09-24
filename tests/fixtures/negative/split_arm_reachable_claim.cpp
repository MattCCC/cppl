// SPEC: CASE-018, CASE-019
// An arm supposes its own case and nothing more. `busy` is possible here, so a
// claim that its path cannot occur is refused; the same claim in the `idle` arm,
// where the precondition contradicts it, would stand.
enum class Mode : unsigned { idle = 0u, busy = 1u };

proof same(unsigned x)
    proves (x == x)
{
    refl;
}

verified unsigned reachable(Mode m)
    expects (m != Mode::idle)
    ensures (result == 1u)
{
    cases m {
        Mode::idle => {
            contradiction same(0u);
        }

        Mode::busy => {
            contradiction same(0u);
        }

        unnamed(value) => {
        }
    }
    return 1u;
}

int main() {
    return 0;
}
