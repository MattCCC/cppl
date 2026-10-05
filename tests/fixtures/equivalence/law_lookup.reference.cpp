// Ordinary C++: `law_lookup.cpp` with its C++L constructs erased, as SPEC.md
// Annex M says they erase. With its Laws gone, every call and every detection
// below resolves as it does in the C++L unit's runtime program.
#include <cstdio>
#include <type_traits>
#include <utility>

struct Seven {
    char bytes[7];
};

Seven pick(long) {
    return Seven{};
}

template <unsigned N> unsigned resolved(unsigned y) {
    return N;
}

template unsigned resolved<sizeof(pick(0))>(unsigned);

Seven area(long) {
    return Seven{};
}

namespace geo {

template <unsigned N> unsigned hidden(unsigned y) {
    return N;
}

unsigned measure_area() {
    return hidden<sizeof(area(0L))>(0u);
}

} // namespace geo

template <class T, class = void> struct coverable : std::integral_constant<unsigned, 2u> {};

template <class T>
struct coverable<T, decltype(void(covers(std::declval<T>())))> : std::integral_constant<unsigned, 1u> {};

template <unsigned N> unsigned detected(unsigned y) {
    return N;
}

unsigned detect() {
    return detected<coverable<unsigned>::value>(0u);
}

namespace {} // namespace

int main() {
    std::printf("%u %u %u\n", resolved<sizeof(pick(0))>(0u), geo::measure_area(), detect());
}
