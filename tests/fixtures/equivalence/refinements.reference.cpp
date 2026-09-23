// Ordinary C++: `refinements.cpp` erased by hand as SPEC.md 36.3 and Annex M say
// it erases. Each refinement is the alias of its base type, an indexed one an
// alias template whose index nothing reads, and every predicate, contract and
// loop clause is gone.
#include <cstddef>
#include <cstdio>
#include <type_traits>
#include <typeinfo>

using NonNegative = int;
using Percentage = NonNegative;
using Small = unsigned;
template <unsigned n> using Index = unsigned;

struct Money {
    unsigned cents;
};

using Positive = Money;

unsigned made = 0u;
unsigned gone = 0u;

struct Tracked {
    unsigned id;

    explicit Tracked(unsigned value) : id(value) {
        ++made;
    }

    Tracked(const Tracked& other) : id(other.id) {
        ++made;
    }

    Tracked& operator=(const Tracked&) = default;

    ~Tracked() {
        ++gone;
    }
};

using Live = Tracked;

struct Reading {
    Percentage level;
    NonNegative count;
    Small digit;
    bool flagged;
};

int keeps(NonNegative n) {
    return n;
}

Percentage fifty() {
    return 50;
}

unsigned cents(Positive p) {
    return p.cents;
}

unsigned identity_of(const Live& live) {
    return live.id;
}

unsigned copied_identity(Live live) {
    return live.id;
}

int reading_level() {
    Reading reading{10, 3, 4u, true};
    reading.level = 70;
    return reading.level;
}

int reading_count(Reading reading) {
    return reading.count;
}

template <unsigned N> Index<N> make_index(unsigned x) {
    return x;
}

unsigned small_loop() {
    Small i = 0u;
    while (i < 9u) {
        ++i;
    }
    return i;
}

int main() {
    const Tracked tracked{5u};
    const unsigned identities = identity_of(tracked) + copied_identity(tracked);
    std::printf("%d %d %u %u %d %u %u\n", keeps(2), fifty(), cents(Money{9u}), identities, reading_level(),
                make_index<4u>(3u), small_loop());
    std::printf("%zu %zu %zu %zu %zu\n", sizeof(Reading), alignof(Reading), offsetof(Reading, count),
                offsetof(Reading, digit), offsetof(Reading, flagged));
    std::printf("%d %d %d %d\n", static_cast<int>(typeid(Percentage) == typeid(int)),
                static_cast<int>(typeid(Index<3u>) == typeid(unsigned)),
                static_cast<int>(std::is_same_v<Live, Tracked>), static_cast<int>(sizeof(Positive) == sizeof(Money)));
    std::printf("%u %u\n", made, gone);
}
