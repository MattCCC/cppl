// Ordinary C++: `path_splits.cpp` erased by hand as SPEC.md ERASE-016 and
// Annex M say it erases. Each case split, and each `decompose`, is an empty
// statement where it stood; the proof, the specifiers and every clause are
// gone; and the explicit instantiation stays, since it is ordinary C++.
#include <cstdio>
#include <optional>

enum class Mode : unsigned { idle = 0u, busy = 1u };
enum class Phase : unsigned { idle = 0u, busy = 1u };

struct Slot {
    Mode mode;
    unsigned count;
};

unsigned settled(Mode m) {
    ;
    return static_cast<unsigned>(m);
}

void rewritten(Mode& m) {
    ;
    m = Mode::busy;
    ;
}

void unaliased(Mode& m, Mode& n) {
    ;
}

void through_reference(Mode& m) {
    Mode& r = m;
    r = Mode::busy;
    ;
}

void make_busy(Mode& m) {
    m = Mode::busy;
}

void after_call(Mode& m) {
    make_busy(m);
    ;
}

unsigned member_binder(const Slot& s) {
    ;
    return static_cast<unsigned>(s.mode);
}

unsigned member_write(unsigned x) {
    Slot s{Mode::idle, x};
    ;
    s.mode = Mode::busy;
    ;
    return s.count;
}

unsigned elements(unsigned i) {
    Mode modes[2] = {Mode::idle, Mode::idle};
    modes[1] = Mode::busy;
    ;
    ;
    modes[i] = Mode::busy;
    ;
    return 1u;
}

unsigned iterated(Mode m, unsigned n) {
    unsigned i = 0u;
    while (i < n) {
        ;
        i = i + 1u;
    }
    return i;
}

unsigned nested(std::optional<Mode> o, const int* p) {
    ;
    ;
    return 1u;
}

unsigned binder_in_claim(const Slot& s) {
    if (s.count == 2u) {
        ;
    }
    return s.count;
}

unsigned unbraced(Mode m, bool flag) {
    if (flag)
        ;
    return 1u;
}

template <typename T> unsigned generic(T m) {
    ;
    return 1u;
}

template unsigned generic<Phase>(Phase);

int main() {
    Mode m = Mode::idle;
    rewritten(m);
    Mode n = Mode::idle;
    Mode o = Mode::busy;
    unaliased(n, o);
    Mode r = Mode::idle;
    through_reference(r);
    Mode c = Mode::idle;
    after_call(c);
    const int x = 4;
    std::printf("%u %u %u %u %u %u %u %u %u %u %u %u\n", settled(Mode::busy), static_cast<unsigned>(m),
                static_cast<unsigned>(r), static_cast<unsigned>(c), member_binder(Slot{Mode::idle, 3u}),
                member_write(5u), elements(1u), iterated(Mode::idle, 3u), nested(Mode::busy, &x),
                binder_in_claim(Slot{Mode::busy, 1u}), unbraced(Mode::busy, false), generic(Mode::idle));
    std::printf("%u\n", generic(Phase::busy));
}
