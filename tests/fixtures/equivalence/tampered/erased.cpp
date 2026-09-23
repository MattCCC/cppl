// Ordinary C++: what `type Small = unsigned where (self < 10u);` and a function
// taking a `Money` must erase to. `checked.cpp` and `tagged.cpp` each add one
// artifact erasure must never introduce, and the equivalence suites require the
// comparison they rely on to tell each of them apart from this file.
#include <cstdio>

using Small = unsigned;

struct Money {
    unsigned cents;
};

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
