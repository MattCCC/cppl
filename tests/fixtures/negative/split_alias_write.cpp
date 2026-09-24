// SPEC: CASE-020
// The refused half of a matched pair whose accepted half is `unaliased` in
// `fixtures/case_split.cpp`; the only difference is the write through `n`. Two
// references may designate one object, so writing `n` leaves nothing known of
// `m`, and no case of it can be omitted.
enum class Mode : unsigned { idle = 0u, busy = 1u };

proof same(unsigned x)
    proves (x == x)
{
    refl;
}

verified void alias_write(Mode& m, Mode& n)
    expects (m == Mode::idle)
{
    n = Mode::busy;
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
