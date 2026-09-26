// cross_tu/library.hpp and library.cpp with every C++L construct erased by
// hand: what C++ programmers would write without C++L. tests/e2e/cross_tu.sh
// compiles this with Clang alone and requires the same code as library.cpp
// compiled by cppl, whatever interface it writes (SPEC.md TUBOUND-002, ABI-001).
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

unsigned zero() {
    return 0u;
}

unsigned read_device();

namespace {
unsigned below_four(unsigned x) {
    if (x < 4u) {
        return x;
    }
    return 3u;
}
} // namespace

unsigned clamp4(unsigned x) {
    return below_four(x);
}

Small small_of(unsigned x) {
    if (x < 4u) {
        return x;
    }
    return 0u;
}

unsigned count_to(unsigned n) {
    unsigned i = 0u;
    while (i < n) {
        ++i;
    }
    return i;
}

unsigned count_up(unsigned n) {
    unsigned i = 0u;
    while (i < n) {
        ++i;
    }
    return i;
}

unsigned never_seven(unsigned x) {
    if (x == 7u) {
        ;
    }
    return x;
}

unsigned sensor() {
    unsigned x = 0u;
    {
        x = read_device();
    }
    if (x > 100u) {
        return 100u;
    }
    return x;
}

void bump(unsigned& counter) {
    counter = counter + 1u;
}

unsigned step(unsigned x) {
    return x;
}

unsigned long step(unsigned long x) {
    return x + 1ul;
}

template <> unsigned bound<4u>(unsigned x) {
    if (x < 4u) {
        return x;
    }
    return 0u;
}

unsigned read_device() {
    return 42u;
}
