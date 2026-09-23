// Ordinary C++: `abi_library.cpp` erased by hand as SPEC.md 36.3, 37 and Annex M
// say it erases. Every refinement is its base type's alias and every contract,
// law and marker is gone.
#include <cstddef>

using NonNegative = int;
using Percentage = NonNegative;
using Small = unsigned;
template <unsigned n> using Index = unsigned;

struct Money {
    unsigned cents;
};

using Positive = Money;

struct Reading {
    Percentage level;
    NonNegative count;
    Small digits[3];
    bool flagged;
};

namespace ledger {

unsigned twice(unsigned x) {
    return x + x;
}

int level_of(Reading reading) {
    return reading.level;
}

int level_through(const Reading& reading) {
    return reading.level;
}

Percentage clamp_percentage(int x) {
    if (x < 0) {
        return 0;
    }
    if (x > 100) {
        return 100;
    }
    return x;
}

unsigned cents(Positive p) {
    return p.cents;
}

Money refund(Money paid) {
    return Money{paid.cents + 1u};
}

Small digit(unsigned x) {
    return x;
}

template <unsigned N> Index<N> make_index(unsigned x) {
    return x;
}

unsigned index_of(unsigned x) {
    return make_index<4u>(x);
}

} // namespace ledger

extern "C" unsigned small_as_c(Small s) {
    return s;
}

std::size_t reading_size() {
    return sizeof(Reading);
}

std::size_t reading_alignment() {
    return alignof(Reading);
}

std::size_t reading_count_offset() {
    return offsetof(Reading, count);
}

std::size_t reading_digits_offset() {
    return offsetof(Reading, digits);
}

std::size_t reading_flagged_offset() {
    return offsetof(Reading, flagged);
}

std::size_t positive_size() {
    return sizeof(Positive);
}
