// SPEC: CASE-020
// The refused half of a matched pair whose accepted half is `after_call` in
// `fixtures/case_split.cpp`. What is known of `m` after the call is the callee's
// postcondition, `busy`, not the entry fact, so omitting `busy` is refused.
enum class Mode : unsigned { idle = 0u, busy = 1u };

proof same(unsigned x)
    proves (x == x)
{
    refl;
}

verified void make_busy(Mode& m)
    ensures (m == Mode::busy)
{
    m = Mode::busy;
}

verified void after_call(Mode& m)
    expects (m == Mode::idle)
{
    make_busy(m);
    cases m {
        Mode::idle => {
        }

        omit Mode::busy by contradiction same(0u);

        omit unnamed by contradiction same(0u);
    }
}

int main() {
    return 0;
}
