// cross_tu/client.cpp with every C++L construct erased by hand, declaring the
// functions of library.cpp and middle.cpp as ordinary C++ does. tests/e2e/
// cross_tu.sh compiles this with Clang alone and requires the same code as
// client.cpp compiled by cppl with both interfaces imported: using another
// unit's contract changes nothing that runs (SPEC.md TUBOUND-003, ERASE-010).
#include <cstdio>

using Small = unsigned;

unsigned clamp4(unsigned x);
Small small_of(unsigned x);
unsigned count_to(unsigned n);
unsigned count_up(unsigned n);
unsigned never_seven(unsigned x);
unsigned sensor();
void bump(unsigned& counter);
unsigned step(unsigned x);
unsigned long step(unsigned long x);

template <unsigned N> unsigned bound(unsigned x);

template <> unsigned bound<4u>(unsigned x);

unsigned doubled(unsigned y);

unsigned clamped(unsigned y) {
    return clamp4(y);
}

unsigned small(unsigned y) {
    return small_of(y);
}

unsigned counted(unsigned n) {
    return count_to(n);
}

unsigned counted_up(unsigned n) {
    return count_up(n);
}

unsigned not_seven(unsigned x) {
    return never_seven(x);
}

unsigned reading() {
    return sensor();
}

unsigned bumped(unsigned start) {
    unsigned counter = start;
    bump(counter);
    return counter;
}

unsigned long stepped(unsigned narrow, unsigned long wide) {
    unsigned kept = step(narrow);
    return step(wide);
}

unsigned bounded(unsigned x) {
    return bound<4u>(x);
}

unsigned through_middle(unsigned y) {
    return doubled(y);
}

unsigned via_local(unsigned y) {
    return clamped(y);
}

unsigned own(unsigned y) {
    return y;
}

int main() {
    unsigned counter = 3u;
    bump(counter);
    std::printf("%u %u %u %u %u %u %u %lu %u %u %u %u\n", clamped(42u), small(2u), counted(5u), counted_up(6u),
                not_seven(3u), reading(), bumped(9u), stepped(1u, 9ul), bounded(7u), through_middle(30u), counter,
                via_local(1u));
    return 0;
}
