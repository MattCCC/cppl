// Refinement declarations and every place a refined type can be named
// (SPEC.md ERASE-004, ERASE-010, ABI-002, 17.8, Annex M).
//
// A refinement is runtime-bearing: it lowers to the alias of its base type and
// nothing else. Erased, this unit must compile to exactly the code
// `refinements.reference.cpp` compiles to, where each declaration was replaced
// by hand with that alias. A wrapper, a tag, a check of the predicate, or any
// change to how a refined value is laid out, passed or destroyed would make the
// two differ.
#include <cstddef>
#include <cstdio>
#include <type_traits>
#include <typeinfo>

type NonNegative = int where (self >= 0);
type Percentage = NonNegative where (self <= 100);
type Small = unsigned where (self < 10u);
type Index(unsigned n) = unsigned where (self < n);

struct Money {
    unsigned cents;
};

type Positive = Money where (self.cents > 0u);

// A base with observable construction and destruction: refining it adds no
// constructor, copy or destructor call.
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

type Live = Tracked where (self.id > 0u);

struct Reading {
    Percentage level;
    NonNegative count;
    Small digit;
    bool flagged;
};

verified int keeps(NonNegative n)
    ensures (result >= 0)
{
    return n;
}

verified Percentage fifty()
    ensures (result == 50)
{
    return 50;
}

verified unsigned cents(Positive p)
    ensures (result > 0u)
{
    return p.cents;
}

verified unsigned identity_of(const Live& live)
    ensures (result > 0u)
{
    return live.id;
}

verified unsigned copied_identity(Live live)
    ensures (result > 0u)
{
    return live.id;
}

verified int reading_level()
    ensures (result == 70)
{
    Reading reading{10, 3, 4u, true};
    reading.level = 70;
    return reading.level;
}

verified int reading_count(Reading reading)
    ensures (result >= 0)
{
    return reading.count;
}

template <unsigned N>
verified Index<N> make_index(unsigned x)
    expects (x < N)
{
    return x;
}

verified unsigned small_loop()
    ensures (result == 9u)
{
    Small i = 0u;
    while (i < 9u)
        invariant (i <= 9u)
    {
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
