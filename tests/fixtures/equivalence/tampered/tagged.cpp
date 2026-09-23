// `erased.cpp` with a validation flag added to the record: the hidden field
// SPEC.md ERASE-007 and ERASEMATRIX-002 forbid. Nothing reads it, so only the
// layout and the calling convention can show it is there.
#include <cstdio>

using Small = unsigned;

struct Money {
    unsigned cents;
    bool validated;
};

Small digit(unsigned x) {
    return x;
}

unsigned cents(Money money) {
    return money.cents;
}

int main(int count, char**) {
    std::printf("%u %u %zu\n", digit(static_cast<unsigned>(count)), cents(Money{static_cast<unsigned>(count), true}),
                sizeof(Money));
}
