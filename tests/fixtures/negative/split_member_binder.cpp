// SPEC: CASE-017, CASE-018
// The refused half of a matched pair whose accepted half is `member_binder` in
// `fixtures/case_split.cpp`. The binder is the member the precondition states is
// idle, so omitting `idle` is refused where omitting `busy` is not.
enum class Mode : unsigned { idle = 0u, busy = 1u };

struct Slot {
    Mode mode;
    unsigned count;
};

proof same(unsigned x)
    proves (x == x)
{
    refl;
}

verified unsigned member_binder(const Slot& s)
    expects (s.mode == Mode::idle)
    ensures (result == 0u)
{
    decompose s {
        components(mode, count) => {
            cases mode {
                Mode::busy => {
                }

                omit Mode::idle by contradiction same(0u);

                omit unnamed by contradiction same(0u);
            }
        }
    }
    return static_cast<unsigned>(s.mode);
}

int main() {
    return 0;
}
