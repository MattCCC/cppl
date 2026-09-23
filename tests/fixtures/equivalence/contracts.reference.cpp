// Ordinary C++: `contracts.cpp` erased by hand as SPEC.md Annex M says it
// erases. `verified` and `pure` are gone and so is every clause; the bodies,
// the loops and the other specifiers are untouched, and each claim that a path
// cannot occur is the empty statement its `;` leaves.
#include <cstdio>

unsigned zero() {
    return 0u;
}

unsigned successor(unsigned x) {
    return x + 1u;
}

unsigned twice(unsigned x) {
    return x + x;
}

static unsigned kept_static(unsigned x) {
    return x;
}

inline unsigned kept_inline(unsigned x) {
    return x;
}

unsigned kept_noexcept(unsigned x) noexcept {
    return x;
}

template <unsigned N> unsigned clamp_to(unsigned x) {
    return x;
}

template <unsigned N> unsigned pick(unsigned x) {
    return x;
}

template <> unsigned pick<4u>(unsigned x) {
    return x;
}

unsigned count_up(unsigned n) {
    unsigned i = 0u;
    while (i < n) {
        i = i + 1u;
    }
    return i;
}

unsigned count_for(unsigned n) {
    unsigned last = 0u;
    for (unsigned i = 0u; i < n; ++i) {
        last += 1u;
    }
    return last;
}

unsigned counted_down(unsigned n) {
    unsigned left = n;
    while (left > 0u) {
        left = left - 1u;
    }
    return left;
}

unsigned unbraced(unsigned x) {
    if (x >= 5u)
        ;
    return x;
}

unsigned after(unsigned x) {
    if (x >= 5u) {
        ;
        return 7u;
    }
    return x;
}

unsigned uses_call(unsigned x) {
    unsigned y = successor(x);
    if (y == 0u) {
        ;
    }
    return y;
}

void set(int& x) {
    x = 1;
}

int main() {
    int written = 0;
    set(written);
    std::printf("%u %u %u %u %u %u %u %u %u %u %u %u %u %d %d\n", successor(3u), twice(4u), kept_static(5u),
                kept_inline(6u), kept_noexcept(7u), clamp_to<4u>(3u) + clamp_to<8u>(7u), pick<4u>(2u) + pick<9u>(8u),
                count_up(5u), count_for(6u), counted_down(4u), unbraced(3u), after(2u), uses_call(3u), written,
                static_cast<int>(noexcept(kept_noexcept(0u))));
}
