// SPEC: CASE-020
// The refused half of a matched pair whose accepted half is `iterated` in
// `fixtures/case_split.cpp`; the only difference is that this loop writes `m`.
// At the loop's head `m` is whatever an iteration left, which the invariant does
// not state, so neither the entry fact nor the split before the loop describes
// it, and omitting `busy` is refused.
enum class Mode : unsigned { idle = 0u, busy = 1u };

proof same(unsigned x)
    proves (x == x)
{
    refl;
}

verified void loop_carried(Mode& m, unsigned n)
    expects (m == Mode::idle)
{
    cases m {
        Mode::idle => {
        }

        omit Mode::busy by contradiction same(0u);

        omit unnamed by contradiction same(0u);
    }
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
    {
        cases m {
            Mode::idle => {
            }

            omit Mode::busy by contradiction same(0u);

            omit unnamed by contradiction same(0u);
        }
        m = Mode::busy;
        i = i + 1u;
    }
}

int main() {
    return 0;
}
