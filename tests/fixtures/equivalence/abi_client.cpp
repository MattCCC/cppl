// Ordinary C++ using `abi_library.cpp` across a translation-unit boundary
// (SPEC.md ABI-001, ABI-002, ABI-003, ABI-005).
//
// Compiled by Clang alone. It declares the library's interface with the base
// types the library's refinements lower to and knows nothing of C++L: linking
// is what checks the mangled names, the calls are what check the calling
// convention, and the layout the library reports is compared with this unit's
// own.
#include <cstddef>
#include <cstdio>

struct Money {
    unsigned cents;
};

struct Reading {
    int level;
    int count;
    unsigned digits[3];
    bool flagged;
};

namespace ledger {
unsigned twice(unsigned x);
int level_of(Reading reading);
int level_through(const Reading& reading);
int clamp_percentage(int x);
unsigned cents(Money p);
Money refund(Money paid);
unsigned digit(unsigned x);
unsigned index_of(unsigned x);
} // namespace ledger

extern "C" unsigned small_as_c(unsigned s);

std::size_t reading_size();
std::size_t reading_alignment();
std::size_t reading_count_offset();
std::size_t reading_digits_offset();
std::size_t reading_flagged_offset();
std::size_t positive_size();

int main() {
    const Reading reading{42, 7, {1u, 2u, 3u}, true};
    std::printf("%u %d %d %d %d %d %u %u %u %u %u\n", ledger::twice(21u), ledger::level_of(reading),
                ledger::level_through(reading), ledger::clamp_percentage(-5), ledger::clamp_percentage(150),
                ledger::clamp_percentage(64), ledger::cents(Money{5u}), ledger::refund(Money{8u}).cents,
                ledger::digit(9u), ledger::index_of(3u), small_as_c(6u));

    const bool layout = reading_size() == sizeof(Reading) && reading_alignment() == alignof(Reading) &&
                        reading_count_offset() == offsetof(Reading, count) &&
                        reading_digits_offset() == offsetof(Reading, digits) &&
                        reading_flagged_offset() == offsetof(Reading, flagged) && positive_size() == sizeof(Money);
    std::printf("%d\n", layout ? 1 : 0);
    return layout ? 0 : 1;
}
