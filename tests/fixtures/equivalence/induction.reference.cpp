// Ordinary C++: `induction.cpp` erased by hand as SPEC.md Annex M says it
// erases. Every law and proof declaration, and every `induction` statement in
// them, is gone, and `pure` leaves its function untouched.
#include <cstdio>

unsigned add(unsigned x, unsigned y) {
    return x + y;
}

unsigned scaled_square(unsigned k, unsigned m) {
    return k * m * m;
}

unsigned sum_below(unsigned n) {
    unsigned total = 0u;
    for (unsigned i = 0u; i < n; ++i) {
        total = add(total, i);
    }
    return total;
}

int main() {
    std::printf("%u %u %u\n", add(5u, 0u), scaled_square(3u, 2u), sum_below(10u));
}
