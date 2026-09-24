// Ordinary C++: `ghost_state.cpp` erased by hand as SPEC.md Annex M says it
// erases. Every ghost declaration is gone, and so are `verified`, `pure` and
// every clause; the statements around them are untouched.
#include <cstdio>

unsigned twice(unsigned x) {
    return x + x;
}

unsigned count(unsigned n) {
    unsigned i = 0u;
    while (i < n) {
        ++i;
    }
    return i;
}

unsigned doubled(unsigned x) {
    unsigned y = x;
    y = y + x;
    return y;
}

int main() {
    std::printf("%u %u\n", count(3u), doubled(2u));
}
