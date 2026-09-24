// SPEC: CASE-002, CASE-017
// Labels are read by the one rule for arms: an enumerator of another
// enumeration names no case of this subject.
enum class Mode : unsigned { idle = 0u, busy = 1u };
enum class Other : unsigned { idle = 0u };

verified unsigned wrong_label(Mode m)
    ensures (result == 0u)
{
    cases m {
        Other::idle => {
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
