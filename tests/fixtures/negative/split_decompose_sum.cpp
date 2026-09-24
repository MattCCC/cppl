// SPEC: CASE-003, CASE-017
// On a runtime path as in a proof body, a sum is split by `cases` and never
// decomposed into components.
enum class Mode : unsigned { idle = 0u, busy = 1u };

verified unsigned decomposed(Mode m)
    ensures (result == 0u)
{
    decompose m {
        components(value) => {
        }
    }
    return 0u;
}

int main() {
    return 0;
}
