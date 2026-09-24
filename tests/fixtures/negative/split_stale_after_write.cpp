// SPEC: CASE-020
// The refused half of a matched pair whose accepted half is `rewritten` in
// `fixtures/case_split.cpp`. After the write `m` is busy, whatever the entry
// fact said of the version before it, so omitting `busy` is refused.
enum class Mode : unsigned { idle = 0u, busy = 1u };

proof same(unsigned x)
    proves (x == x)
{
    refl;
}

verified void stale_after_write(Mode& m)
    expects (m == Mode::idle)
{
    m = Mode::busy;
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
