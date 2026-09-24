// SPEC: CASE-020
// The refused half of a matched pair whose accepted half is `elements` in
// `fixtures/case_split.cpp`. The write at `modes[i]` may be to `modes[0]`, which
// is then unknown: not idle, not busy, not even one of the two. Omitting its
// residual case is refused, though it held before the write.
enum class Mode : unsigned { idle = 0u, busy = 1u };

proof same(unsigned x)
    proves (x == x)
{
    refl;
}

verified unsigned elements(unsigned i)
    expects (i < 2u)
    ensures (result == 1u)
{
    Mode modes[2] = {Mode::idle, Mode::idle};
    modes[i] = Mode::busy;
    cases modes[0] {
        Mode::idle => {
        }

        Mode::busy => {
        }

        omit unnamed by contradiction same(0u);
    }
    return 1u;
}

int main() {
    return 0;
}
