// `erased.cpp` with the refinement predicate checked at run time: the hidden
// validation SPEC.md ERASE-007 and ERASE-010 forbid. The check passes for every
// input the program is run with, so only the code can show it is there.
#include <cstdio>

using Small = unsigned;

struct Money {
    unsigned cents;
};

Small digit(unsigned x) {
    if (x >= 10u) {
        __builtin_trap();
    }
    return x;
}

unsigned cents(Money money) {
    return money.cents;
}

int main(int count, char**) {
    std::printf("%u %u %zu\n", digit(static_cast<unsigned>(count)), cents(Money{static_cast<unsigned>(count)}),
                sizeof(Money));
}
