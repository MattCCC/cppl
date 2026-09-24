// SPEC: CASE-020
// The refused half of a matched pair whose accepted half is `member_write` in
// `fixtures/case_split.cpp`. After the write to the member, the split before it
// says nothing: the member is busy, so omitting `busy` is refused.
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

verified unsigned member_write(unsigned x)
    ensures (result == x)
{
    Slot s{Mode::idle, x};
    cases s.mode {
        Mode::idle => {
        }

        omit Mode::busy by contradiction same(0u);

        omit unnamed by contradiction same(0u);
    }
    s.mode = Mode::busy;
    cases s.mode {
        Mode::idle => {
        }

        omit Mode::busy by contradiction same(0u);

        omit unnamed by contradiction same(0u);
    }
    return s.count;
}

int main() {
    return 0;
}
