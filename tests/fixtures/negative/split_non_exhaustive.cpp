// SPEC: CASE-004, CASE-018
// A split on a runtime path accounts for every case exactly as a proof-side one
// does. Dropping `busy` would drop that path unchecked, so it is refused.
enum class Mode : unsigned { idle = 0u, busy = 1u };

verified unsigned missing(Mode m)
    ensures (result == 0u)
{
    cases m {
        Mode::idle => {
        }

        unnamed(value) => {
        }
    }
    return 0u;
}

int main() {
    return 0;
}
