// `erased.cpp` with a proof-status declaration added that nothing uses: a
// runtime artifact SPEC.md ERASEMATRIX-002 forbids, which no code is emitted for.
// Identical code cannot show it is there; only the program's text can, which is
// why the equivalence suites compare that as well.
#include <cstdio>

using Small = unsigned;

struct Money {
    unsigned cents;
};

inline bool digit_is_proven() {
    return true;
}

Small digit(unsigned x) {
    return x;
}

unsigned cents(Money money) {
    return money.cents;
}

int main(int count, char**) {
    std::printf("%u %u %zu\n", digit(static_cast<unsigned>(count)), cents(Money{static_cast<unsigned>(count)}),
                sizeof(Money));
}
